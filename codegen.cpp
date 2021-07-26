#include "global.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

#include <iostream>

using namespace llvm;

static LLVMContext *context;
static IRBuilder<> *builder;
static Module *module_;

static Function *malloc_func;
static Function *free_func;
static FunctionType *dtor_type;
static Function *base_dtor;
static Function *list_dtor;
static StructType *box_header_type;
static Type *box_type;
static StructType *tagged_header_type;
static StructType *cell_type;
static StructType *list_type;
static Function *box_rc_decr;
static Function *cell_rc_decr;

static StructType *struct_type(ArrayRef<Type *> elems) {
    return StructType::get(*context, elems);
}

#define pointer_type PointerType::getUnqual
#define i1_type Type::getInt1Ty(*context)
#define i64_type Type::getInt64Ty(*context)
#define cg_i32 builder->getInt32
#define cg_i64 builder->getInt64

#define cg_bitcast builder->CreateBitCast
#define cg_sgep builder->CreateStructGEP
#define cg_load builder->CreateLoad
#define cg_store builder->CreateStore
#define cg_br builder->CreateBr
#define cg_cond_br builder->CreateCondBr
#define cg_phi builder->CreatePHI
#define cg_call builder->CreateCall
#define cg_ret builder->CreateRet
#define cg_ret_void builder->CreateRetVoid
#define cg_add builder->CreateAdd
#define cg_mul builder->CreateMul
#define cg_icmp_eq builder->CreateICmpEQ

static Value *cg_gep(Value *val, vector<Value *> idx_) {
    vector<Value *> idx = {cg_i64(0)};
    for (Value *v : idx_)
        idx.push_back(v);
    return builder->CreateInBoundsGEP(val, idx);
}

static BasicBlock *create_basic_block(Function *func) {
    return BasicBlock::Create(*context, "", func);
}

static Function *create_function(FunctionType *ftype) {
    return Function::Create(ftype, Function::PrivateLinkage, "", module_);
}

extern "C" BasicBlock *get_insert_block() {
    return builder->GetInsertBlock();
}

extern "C" void set_insert_block(BasicBlock *bb) {
    builder->SetInsertPoint(bb);
}

extern "C" void llvm_init() {
    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmParsers();
    InitializeAllAsmPrinters();
    string target_triple = sys::getDefaultTargetTriple();
    string error;
    const Target *target = TargetRegistry::lookupTarget(target_triple, error);
    if (!target) {
        errs() << "Error looking up target: " << error << "\n";
        exit(1);
    }
    TargetMachine *target_machine = target->createTargetMachine(target_triple, "generic", "", TargetOptions(), Optional<Reloc::Model>(Reloc::PIC_));
    DataLayout data_layout = target_machine->createDataLayout();
    context = new LLVMContext();
    SMDiagnostic err;
    module_ = parseIRFile("module.ll", err, *context).release();
    if (!module_) {
        errs() << "Error initializing module\n";
        err.print("hagane", errs());
        exit(1);
    }
    if (verifyModule(*module_, &errs()) == true) {
        exit(1);
    }
    builder = new IRBuilder<>(*context);
    module_->setDataLayout(data_layout);
    module_->setTargetTriple(target_triple);
    Function *main = Function::Create(FunctionType::get(Type::getVoidTy(*context), false), Function::ExternalLinkage, "hagane_main", module_);
    set_insert_block(create_basic_block(main));
    malloc_func = module_->getFunction("malloc");
    free_func = module_->getFunction("free");
    box_header_type = StructType::getTypeByName(*context, "Box");
    box_type = pointer_type(box_header_type);
    tagged_header_type = struct_type({box_header_type, i64_type});
    dtor_type = FunctionType::get(Type::getVoidTy(*context), {box_type}, false);
    base_dtor = (Function *)cg_bitcast(free_func, pointer_type(dtor_type));
    list_dtor = (Function *)module_->getFunction("list_dtor");
    cell_type = StructType::getTypeByName(*context, "Cell");
    list_type = StructType::getTypeByName(*context, "List");
    box_rc_decr = module_->getFunction("box_rc_decr");
    cell_rc_decr = module_->getFunction("cell_rc_decr");
}

extern "C" BasicBlock *create_block() {
    return create_basic_block(get_insert_block()->getParent());
}

static FunctionType *get_function_type(u64 num_params) {
    return FunctionType::get(box_type, vector<Type *>(num_params + 1, box_type), false);
}

extern "C" Function *begin_new_function(u64 num_params) {
    Function *func = create_function(get_function_type(num_params));
    set_insert_block(create_basic_block(func));
    return func;
}

static Value *codegen_malloc(Value *size, Type *type) {
    Value *val = cg_call(malloc_func->getFunctionType(), malloc_func, {size});
    return cg_bitcast(val, pointer_type(type));
}

static Value *codegen_malloc(Type *type) {
    Value *val = cg_call(malloc_func->getFunctionType(), malloc_func, {ConstantExpr::getSizeOf(type)});
    return cg_bitcast(val, pointer_type(type));
}

static void codegen_box_header_init(Value *val, Value *dtor) {
    val = cg_bitcast(val, pointer_type(box_header_type));
    cg_store(cg_i64(0), cg_sgep(val, 0));
    cg_store(dtor, cg_sgep(val, 1));
}

extern "C" void codegen_rc_incr(Value *val) {
    Value *rc_ptr = cg_sgep(val, 0);
    Value *rc = cg_load(rc_ptr);
    cg_store(cg_add(rc, cg_i64(1)), rc_ptr);
}

extern "C" void codegen_rc_decr(Value *val) {
    cg_call(box_rc_decr->getFunctionType(), box_rc_decr, {val});
}

static Value *codegen_boxed_fuction(Function *func) {
    Type *type = struct_type({box_header_type, pointer_type(func->getFunctionType())});
    Value *val = codegen_malloc(type);
    codegen_box_header_init(val, base_dtor);
    cg_store(func, cg_sgep(val, 1));
    return cg_bitcast(val, box_type);
}

extern "C" Value *codegen_const_bool(u64 value) {
    Value *val = codegen_malloc(struct_type({box_header_type, i1_type}));
    codegen_box_header_init(val, base_dtor);
    cg_store(builder->getInt1(value), cg_sgep(val, 1));
    return cg_bitcast(val, box_type);
}

extern "C" Value *codegen_const_int(u64 value) {
    Value *val = codegen_malloc(struct_type({box_header_type, i64_type}));
    codegen_box_header_init(val, base_dtor);
    cg_store(cg_i64(value), cg_sgep(val, 1));
    return cg_bitcast(val, box_type);
}

static Value *codegen_tuple_(vector<Value *> elems, i64 tag) {
    // Create type
    vector<Type *> type_elems = {tag >= 0 ? tagged_header_type : box_header_type};
    for (size_t i = 0; i < elems.size(); i++)
        type_elems.push_back(box_type);
    Type *type = struct_type(type_elems);

    // Create destructor
    BasicBlock *saved_bb = get_insert_block();
    Function *dtor = Function::Create(dtor_type, Function::PrivateLinkage, "dtor", module_);
    set_insert_block(create_basic_block(dtor));
    Value *dtor_val = cg_bitcast(dtor->getArg(0), pointer_type(type));
    for (size_t i = 0; i < elems.size(); i++)
        codegen_rc_decr(cg_load(cg_sgep(dtor_val, 1 + i)));
    cg_call(free_func->getFunctionType(), free_func, {cg_bitcast(dtor_val, Type::getInt8PtrTy(*context))});
    cg_ret_void();
    set_insert_block(saved_bb);

    // Build tuple
    Value *val = codegen_malloc(type);
    codegen_box_header_init(val, dtor);
    if (tag >= 0)
        cg_store(cg_i64(tag), cg_gep(val, {cg_i32(0), cg_i32(1)}));
    for (size_t i = 0; i < elems.size(); i++)
        cg_store(elems[i], cg_sgep(val, 1 + i));
    return cg_bitcast(val, box_type);
}

extern "C" Value *codegen_tuple(vector<Value *> *elems) {
    return codegen_tuple_(*elems, -1);
}

extern "C" Value *codegen_list(vector<Value *> *elems) {
    Value *data_size = cg_mul(ConstantExpr::getSizeOf(box_type), cg_i64(elems->size()));
    Value *val = codegen_malloc(cg_add(ConstantExpr::getOffsetOf(list_type, 3), data_size), list_type);
    codegen_box_header_init(val, list_dtor);
    cg_store(cg_i64(elems->size()), cg_sgep(val, 1));
    cg_store(cg_i64(elems->size()), cg_sgep(val, 2));
    for (size_t i = 0; i < elems->size(); i++) {
        cg_store((*elems)[i], cg_gep(val, {cg_i32(3), cg_i64(i)}));
    }
    return val;
}

extern "C" void codegen_cell_rc_incr(Value *ptr) {
    Value *rc_ptr = cg_sgep(ptr, 0);
    cg_store(cg_add(cg_load(rc_ptr), cg_i64(1)), rc_ptr);
}

extern "C" void codegen_cell_rc_decr(Value *ptr) {
    cg_call(cell_rc_decr->getFunctionType(), cell_rc_decr, {ptr});
}

extern "C" Value *codegen_create_mut_var(Value *val) {
    Value *ptr = codegen_malloc(cell_type);
    cg_store(cg_i64(0), cg_sgep(ptr, 0));
    cg_store(val, cg_sgep(ptr, 1));
    return ptr;
}

extern "C" Value *codegen_load_mut_var(Value *ptr) {
    return cg_load(cg_sgep(ptr, 1));
}

extern "C" void codegen_store_mut_var(Value *ptr, Value *val) {
    cg_store(val, cg_sgep(ptr, 1));
}

extern "C" void codegen_unreachable() {
    builder->CreateUnreachable();
}

extern "C" void codegen_br(BasicBlock *bb) {
    cg_br(bb);
}

extern "C" void codegen_cond_br(Value *cond, BasicBlock *then, BasicBlock *else_) {
    cond = cg_bitcast(cond, pointer_type(struct_type({box_header_type, i1_type})));
    Value *cond_i1 = cg_load(cg_sgep(cond, 1));
    cg_cond_br(cond_i1, then, else_);
}

extern "C" PHINode *codegen_phi(u64 reserved) {
    return cg_phi(box_type, reserved);
}

extern "C" void phi_add_incoming(PHINode *phi, Value *val, BasicBlock *bb) {
    phi->addIncoming(val, bb);
}

extern "C" Value *codegen_apply(Value *func_val, vector<Value *> *args) {
    FunctionType *func_type = get_function_type(args->size());
    Type *func_header_type = struct_type({box_header_type, pointer_type(func_type)});
    Value *func = cg_load(cg_sgep(cg_bitcast(func_val, pointer_type(func_header_type)), 1));
    args->push_back(func_val);
    Value *val = cg_call(func_type, func, *args);
    args->pop_back();
    return val;
}

extern "C" Value *codegen_get_arg(u64 i) {
    return get_insert_block()->getParent()->getArg(i);
}

extern "C" Type *get_captures_type(u64 num_params, vector<Value *> captures) {
    vector<Type *> elem_types = {struct_type({box_header_type, pointer_type(get_function_type(num_params))})};
    for (Value *capture : captures)
        elem_types.push_back(capture->getType());
    return StructType::create(*context, elem_types, "Func");
}

extern "C" Value *codegen_get_captures(Type *captures_type) {
    Function *func = get_insert_block()->getParent();
    return cg_bitcast(func->getArg(func->arg_size() - 1), pointer_type(captures_type));
}

extern "C" Value *codegen_extract_capture(Value *captures, u64 i) {
    return cg_load(cg_sgep(captures, 1 + i));
}

extern "C" void codegen_ret(Value *val) {
    cg_ret(val);
}

extern "C" Value *codegen_func_val(Function *func, vector<Value *> *captures, Type *type) {
    // Create destructor
    BasicBlock *saved_bb = get_insert_block();
    Function *dtor = Function::Create(dtor_type, Function::PrivateLinkage, "dtor", module_);
    set_insert_block(create_basic_block(dtor));
    Value *dtor_val = cg_bitcast(dtor->getArg(0), pointer_type(type));
    for (size_t i = 0; i < captures->size(); i++) {
        Value *capture = cg_load(cg_sgep(dtor_val, 1 + i));
        if (capture->getType() == box_type)
            codegen_rc_decr(capture);
        else
            codegen_cell_rc_decr(capture);
    }
    cg_call(free_func->getFunctionType(), free_func, {cg_bitcast(dtor_val, Type::getInt8PtrTy(*context))});
    cg_ret_void();
    set_insert_block(saved_bb);

    // Build value
    Value *val = codegen_malloc(type);
    codegen_box_header_init(val, dtor);
    cg_store(func, cg_gep(val, {cg_i32(0), cg_i32(1)}));
    for (size_t i = 0; i < captures->size(); i++)
        cg_store((*captures)[i], cg_sgep(val, 1 + i));
    return cg_bitcast(val, box_type);
}

static Value *codegen_create_ctor_(u64 num_params, i64 tag) {
    FunctionType *ctor_type = get_function_type(num_params);
    BasicBlock *saved_bb = get_insert_block();
    Function *ctor = Function::Create(ctor_type, Function::PrivateLinkage, "ctor", module_);
    set_insert_block(create_basic_block(ctor));
    vector<Value *> elems;
    for (u64 i = 0; i < num_params; i++)
        elems.push_back(ctor->getArg(i));
    cg_ret(codegen_tuple_(elems, tag));
    set_insert_block(saved_bb);
    return codegen_boxed_fuction(ctor);
}

extern "C" Value *codegen_create_tagless_ctor(u64 num_params) {
    return codegen_create_ctor_(num_params, -1);
}

extern "C" Value *codegen_create_tagged_ctor(u64 num_params, u64 tag) {
    return codegen_create_ctor_(num_params, tag);
}

struct PatternEnd {
    BasicBlock *src_block;
    Value *cond_val;
    BasicBlock *success_block;
};

extern "C" void bind_pattern_end(PatternEnd *pattern_end, BasicBlock *fail_block) {
    BasicBlock *saved_bb = get_insert_block();
    set_insert_block(pattern_end->src_block);
    cg_cond_br(pattern_end->cond_val, pattern_end->success_block, fail_block);
    set_insert_block(saved_bb);
}

extern "C" PatternEnd *codegen_int_pattern_test(Value *val, u64 n) {
    val = cg_bitcast(val, pointer_type(struct_type({box_header_type, i64_type})));
    Value *val_i64 = cg_load(cg_sgep(val, 1));
    Value *eq_test = cg_icmp_eq(val_i64, cg_i64(n));
    BasicBlock *parent_block = get_insert_block();
    BasicBlock *success_block = create_basic_block(parent_block->getParent());
    set_insert_block(success_block);
    return new PatternEnd {parent_block, eq_test, success_block};
}

extern "C" Value *codegen_tuple_cast(Value *val, u64 n) {
    vector<Type *> type_elems = {box_header_type};
    for (u64 i = 0; i < n; i++)
        type_elems.push_back(box_type);
    Type *type = pointer_type(struct_type(type_elems));
    return cg_bitcast(val, type);
}

extern "C" Value *codegen_tagged_cast(Value *val, u64 n) {
    vector<Type *> type_elems = {tagged_header_type};
    for (u64 i = 0; i < n; i++)
        type_elems.push_back(box_type);
    Type *type = pointer_type(struct_type(type_elems));
    return cg_bitcast(val, type);
}

extern "C" PatternEnd *codegen_tag_check(Value *val, u64 tag) {
    Value *val_tag = cg_load(cg_gep(val, {cg_i32(0), cg_i32(1)}));
    Value *tag_test = cg_icmp_eq(val_tag, cg_i64(tag));
    BasicBlock *parent_block = get_insert_block();
    BasicBlock *success_block = create_basic_block(parent_block->getParent());
    set_insert_block(success_block);
    return new PatternEnd {parent_block, tag_test, success_block};
}

extern "C" Value *codegen_tuple_get(Value *val, u64 i) {
    return cg_load(cg_sgep(val, 1 + i));
}

extern "C" Value *codegen_list_cast(Value *val) {
    return cg_bitcast(val, pointer_type(list_type));
}

extern "C" PatternEnd *codegen_len_check(Value *val, u64 len) {
    Value *val_len = cg_load(cg_sgep(val, 1));
    Value *len_test = cg_icmp_eq(val_len, cg_i64(len));
    BasicBlock *parent_block = get_insert_block();
    BasicBlock *success_block = create_basic_block(parent_block->getParent());
    set_insert_block(success_block);
    return new PatternEnd {parent_block, len_test, success_block};
}

extern "C" Value *codegen_list_get(Value *val, u64 i) {
    return cg_load(cg_gep(val, {cg_i32(3), cg_i64(i)}));
}

extern "C" Value *codegen_extern(vector<u64> *name_vector, u64 params, u64 ret_int) {
    string name;
    for (u64 c : *name_vector)
        name.push_back(c);
    if (module_->getFunction(name) != nullptr) {
        cerr << "Error: function named " << name << " already exists\n";
        exit(1);
    }
    Function *extern_func = Function::Create(
        FunctionType::get(
            ret_int ? i64_type : Type::getVoidTy(*context),
            vector<Type *>(params, i64_type),
            false),
        Function::ExternalLinkage, name, module_);
    BasicBlock *saved_bb = get_insert_block();
    Function *func = create_function(get_function_type(params));
    set_insert_block(create_basic_block(func));
    vector<Value *> extern_func_args;
    for (u64 i = 0; i < params; i++) {
        Value *arg = cg_bitcast(func->getArg(i), pointer_type(struct_type({box_header_type, i64_type})));
        extern_func_args.push_back(cg_load(cg_sgep(arg, 1)));
    }
    Value *extern_func_ret = cg_call(extern_func->getFunctionType(), extern_func, extern_func_args);
    if (ret_int) {
        Value *ret = codegen_malloc(struct_type({box_header_type, i64_type}));
        codegen_box_header_init(ret, base_dtor);
        cg_store(extern_func_ret, cg_sgep(ret, 1));
        cg_ret(cg_bitcast(ret, box_type));
    } else {
        cg_ret(codegen_tuple_({}, -1));
    }
    set_insert_block(saved_bb);
    return codegen_boxed_fuction(func);
}

extern "C" void llvm_fin() {
    cg_ret_void();
    module_->print(outs(), nullptr);
    if (verifyModule(*module_, &errs()) == true) {
        exit(1);
    }
}
