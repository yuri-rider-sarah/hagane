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

static Value *cg_gep(Value *val, vector<Value *> idx_) {
    vector<Value *> idx = {builder->getInt64(0)};
    for (Value *v : idx_)
        idx.push_back(v);
    return builder->CreateInBoundsGEP(val, idx);
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
    builder->SetInsertPoint(BasicBlock::Create(*context, "", main));
    malloc_func = module_->getFunction("malloc");
    free_func = module_->getFunction("free");
    box_header_type = StructType::getTypeByName(*context, "Box");
    box_type = PointerType::getUnqual(box_header_type);
    tagged_header_type = StructType::get(*context, {box_header_type, Type::getInt64Ty(*context)});
    dtor_type = FunctionType::get(Type::getVoidTy(*context), {box_type}, false);
    base_dtor = (Function *)builder->CreateBitCast(free_func, PointerType::getUnqual(dtor_type));
    list_dtor = (Function *)module_->getFunction("list_dtor");
    cell_type = StructType::getTypeByName(*context, "Cell");
    list_type = StructType::getTypeByName(*context, "List");
    box_rc_decr = module_->getFunction("box_rc_decr");
    cell_rc_decr = module_->getFunction("cell_rc_decr");
}

extern "C" BasicBlock *get_insert_block() {
    return builder->GetInsertBlock();
}

extern "C" void set_insert_block(BasicBlock *bb) {
    builder->SetInsertPoint(bb);
}

extern "C" BasicBlock *create_block() {
    return BasicBlock::Create(*context, "", builder->GetInsertBlock()->getParent());
}

static FunctionType *get_function_type(u64 num_params) {
    return FunctionType::get(box_type, vector<Type *>(num_params + 1, box_type), false);
}

extern "C" Function *begin_new_function(u64 num_params) {
    Function *func = Function::Create(get_function_type(num_params), Function::PrivateLinkage, "", module_);
    builder->SetInsertPoint(BasicBlock::Create(*context, "", func));
    return func;
}

static Value *codegen_malloc(Value *size, Type *type) {
    Value *val = builder->CreateCall(malloc_func->getFunctionType(), malloc_func, {size});
    return builder->CreateBitCast(val, PointerType::getUnqual(type));
}

static Value *codegen_malloc(Type *type) {
    Value *val = builder->CreateCall(malloc_func->getFunctionType(), malloc_func, {ConstantExpr::getSizeOf(type)});
    return builder->CreateBitCast(val, PointerType::getUnqual(type));
}

static void codegen_box_header_init(Value *val, Value *dtor) {
    val = builder->CreateBitCast(val, PointerType::getUnqual(box_header_type));
    builder->CreateStore(builder->getInt64(0), builder->CreateStructGEP(val, 0));
    builder->CreateStore(dtor, builder->CreateStructGEP(val, 1));
}

extern "C" void codegen_rc_incr(Value *val) {
    Value *rc_ptr = builder->CreateStructGEP(val, 0);
    Value *rc = builder->CreateLoad(rc_ptr);
    builder->CreateStore(builder->CreateAdd(rc, builder->getInt64(1)), rc_ptr);
}

extern "C" void codegen_rc_decr(Value *val) {
    builder->CreateCall(box_rc_decr->getFunctionType(), box_rc_decr, {val});
}

static Value *codegen_boxed_fuction(Function *func) {
    Type *type = StructType::get(*context, {box_header_type, PointerType::getUnqual(func->getFunctionType())});
    Value *val = codegen_malloc(type);
    codegen_box_header_init(val, base_dtor);
    builder->CreateStore(func, builder->CreateStructGEP(val, 1));
    return builder->CreateBitCast(val, box_type);
}

extern "C" Value *codegen_const_bool(u64 value) {
    Value *val = codegen_malloc(StructType::get(*context, {box_header_type, Type::getInt1Ty(*context)}));
    codegen_box_header_init(val, base_dtor);
    builder->CreateStore(builder->getInt1(value), builder->CreateStructGEP(val, 1));
    return builder->CreateBitCast(val, box_type);
}

extern "C" Value *codegen_const_int(u64 value) {
    Value *val = codegen_malloc(StructType::get(*context, {box_header_type, Type::getInt64Ty(*context)}));
    codegen_box_header_init(val, base_dtor);
    builder->CreateStore(builder->getInt64(value), builder->CreateStructGEP(val, 1));
    return builder->CreateBitCast(val, box_type);
}

static Value *codegen_tuple_(vector<Value *> elems, i64 tag) {
    // Create type
    vector<Type *> type_elems = {tag >= 0 ? tagged_header_type : box_header_type};
    for (size_t i = 0; i < elems.size(); i++)
        type_elems.push_back(box_type);
    Type *type = StructType::get(*context, type_elems);

    // Create destructor
    BasicBlock *saved_bb = builder->GetInsertBlock();
    Function *dtor = Function::Create(dtor_type, Function::PrivateLinkage, "dtor", module_);
    builder->SetInsertPoint(BasicBlock::Create(*context, "", dtor));
    Value *dtor_val = builder->CreateBitCast(dtor->getArg(0), PointerType::getUnqual(type));
    for (size_t i = 0; i < elems.size(); i++)
        codegen_rc_decr(builder->CreateLoad(builder->CreateStructGEP(dtor_val, 1 + i)));
    builder->CreateCall(free_func->getFunctionType(), free_func, {builder->CreateBitCast(dtor_val, Type::getInt8PtrTy(*context))});
    builder->CreateRetVoid();
    builder->SetInsertPoint(saved_bb);

    // Build tuple
    Value *val = codegen_malloc(type);
    codegen_box_header_init(val, dtor);
    if (tag >= 0)
        builder->CreateStore(builder->getInt64(tag), cg_gep(val, {builder->getInt32(0), builder->getInt32(1)}));
    for (size_t i = 0; i < elems.size(); i++)
        builder->CreateStore(elems[i], builder->CreateStructGEP(val, 1 + i));
    return builder->CreateBitCast(val, box_type);
}

extern "C" Value *codegen_tuple(vector<Value *> *elems) {
    return codegen_tuple_(*elems, -1);
}

extern "C" Value *codegen_list(vector<Value *> *elems) {
    Value *data_size = builder->CreateMul(ConstantExpr::getSizeOf(box_type), builder->getInt64(elems->size()));
    Value *val = codegen_malloc(builder->CreateAdd(ConstantExpr::getOffsetOf(list_type, 3), data_size), list_type);
    codegen_box_header_init(val, list_dtor);
    builder->CreateStore(builder->getInt64(elems->size()), builder->CreateStructGEP(val, 1));
    builder->CreateStore(builder->getInt64(elems->size()), builder->CreateStructGEP(val, 2));
    for (size_t i = 0; i < elems->size(); i++) {
        builder->CreateStore((*elems)[i], cg_gep(val, {builder->getInt32(3), builder->getInt64(i)}));
    }
    return val;
}

extern "C" void codegen_cell_rc_incr(Value *ptr) {
    Value *rc_ptr = builder->CreateStructGEP(ptr, 0);
    builder->CreateStore(builder->CreateAdd(builder->CreateLoad(rc_ptr), builder->getInt64(1)), rc_ptr);
}

extern "C" void codegen_cell_rc_decr(Value *ptr) {
    builder->CreateCall(cell_rc_decr->getFunctionType(), cell_rc_decr, {ptr});
}

extern "C" Value *codegen_create_mut_var(Value *val) {
    Value *ptr = codegen_malloc(cell_type);
    builder->CreateStore(builder->getInt64(0), builder->CreateStructGEP(ptr, 0));
    builder->CreateStore(val, builder->CreateStructGEP(ptr, 1));
    return ptr;
}

extern "C" Value *codegen_load_mut_var(Value *ptr) {
    return builder->CreateLoad(builder->CreateStructGEP(ptr, 1));
}

extern "C" void codegen_store_mut_var(Value *ptr, Value *val) {
    builder->CreateStore(val, builder->CreateStructGEP(ptr, 1));
}

extern "C" void codegen_unreachable() {
    builder->CreateUnreachable();
}

extern "C" void codegen_br(BasicBlock *bb) {
    builder->CreateBr(bb);
}

extern "C" void codegen_cond_br(Value *cond, BasicBlock *then, BasicBlock *else_) {
    cond = builder->CreateBitCast(cond, PointerType::getUnqual(StructType::get(*context, {box_header_type, Type::getInt1Ty(*context)})));
    Value *cond_i1 = builder->CreateLoad(builder->CreateStructGEP(cond, 1));
    builder->CreateCondBr(cond_i1, then, else_);
}

extern "C" PHINode *codegen_phi(u64 reserved) {
    return builder->CreatePHI(box_type, reserved);
}

extern "C" void phi_add_incoming(PHINode *phi, Value *val, BasicBlock *bb) {
    phi->addIncoming(val, bb);
}

extern "C" Value *codegen_apply(Value *func_val, vector<Value *> *args) {
    FunctionType *func_type = get_function_type(args->size());
    Type *func_header_type = StructType::get(*context, {box_header_type, PointerType::getUnqual(func_type)});
    Value *func = builder->CreateLoad(builder->CreateStructGEP(builder->CreateBitCast(func_val, PointerType::getUnqual(func_header_type)), 1));
    args->push_back(func_val);
    Value *val = builder->CreateCall(func_type, func, *args);
    args->pop_back();
    return val;
}

extern "C" Value *codegen_get_arg(u64 i) {
    return builder->GetInsertBlock()->getParent()->getArg(i);
}

extern "C" Type *get_captures_type(u64 num_params, vector<Value *> captures) {
    vector<Type *> elem_types = {StructType::get(*context, {box_header_type, PointerType::getUnqual(get_function_type(num_params))})};
    for (Value *capture : captures)
        elem_types.push_back(capture->getType());
    return StructType::create(*context, elem_types, "Func");
}

extern "C" Value *codegen_get_captures(Type *captures_type) {
    Function *func = builder->GetInsertBlock()->getParent();
    return builder->CreateBitCast(func->getArg(func->arg_size() - 1), PointerType::getUnqual(captures_type));
}

extern "C" Value *codegen_extract_capture(Value *captures, u64 i) {
    return builder->CreateLoad(builder->CreateStructGEP(captures, 1 + i));
}

extern "C" void codegen_ret(Value *val) {
    builder->CreateRet(val);
}

extern "C" Value *codegen_func_val(Function *func, vector<Value *> *captures, Type *type) {
    // Create destructor
    BasicBlock *saved_bb = builder->GetInsertBlock();
    Function *dtor = Function::Create(dtor_type, Function::PrivateLinkage, "dtor", module_);
    builder->SetInsertPoint(BasicBlock::Create(*context, "", dtor));
    Value *dtor_val = builder->CreateBitCast(dtor->getArg(0), PointerType::getUnqual(type));
    for (size_t i = 0; i < captures->size(); i++) {
        Value *capture = builder->CreateLoad(builder->CreateStructGEP(dtor_val, 1 + i));
        if (capture->getType() == box_type)
            codegen_rc_decr(capture);
        else
            codegen_cell_rc_decr(capture);
    }
    builder->CreateCall(free_func->getFunctionType(), free_func, {builder->CreateBitCast(dtor_val, Type::getInt8PtrTy(*context))});
    builder->CreateRetVoid();
    builder->SetInsertPoint(saved_bb);

    // Build value
    Value *val = codegen_malloc(type);
    codegen_box_header_init(val, dtor);
    builder->CreateStore(func, cg_gep(val, {builder->getInt32(0), builder->getInt32(1)}));
    for (size_t i = 0; i < captures->size(); i++)
        builder->CreateStore((*captures)[i], builder->CreateStructGEP(val, 1 + i));
    return builder->CreateBitCast(val, box_type);
}

static Value *codegen_create_ctor_(u64 num_params, i64 tag) {
    FunctionType *ctor_type = get_function_type(num_params);
    BasicBlock *saved_bb = builder->GetInsertBlock();
    Function *ctor = Function::Create(ctor_type, Function::PrivateLinkage, "ctor", module_);
    builder->SetInsertPoint(BasicBlock::Create(*context, "", ctor));
    vector<Value *> elems;
    for (u64 i = 0; i < num_params; i++)
        elems.push_back(ctor->getArg(i));
    builder->CreateRet(codegen_tuple_(elems, tag));
    builder->SetInsertPoint(saved_bb);
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
    BasicBlock *saved_bb = builder->GetInsertBlock();
    builder->SetInsertPoint(pattern_end->src_block);
    builder->CreateCondBr(pattern_end->cond_val, pattern_end->success_block, fail_block);
    builder->SetInsertPoint(saved_bb);
}

extern "C" PatternEnd *codegen_int_pattern_test(Value *val, u64 n) {
    val = builder->CreateBitCast(val, PointerType::getUnqual(StructType::get(*context, {box_header_type, Type::getInt64Ty(*context)})));
    Value *val_i64 = builder->CreateLoad(builder->CreateStructGEP(val, 1));
    Value *eq_test = builder->CreateICmpEQ(val_i64, builder->getInt64(n));
    BasicBlock *parent_block = builder->GetInsertBlock();
    BasicBlock *success_block = BasicBlock::Create(*context, "", parent_block->getParent());
    builder->SetInsertPoint(success_block);
    return new PatternEnd {parent_block, eq_test, success_block};
}

extern "C" Value *codegen_tuple_cast(Value *val, u64 n) {
    vector<Type *> type_elems = {box_header_type};
    for (u64 i = 0; i < n; i++)
        type_elems.push_back(box_type);
    Type *type = PointerType::getUnqual(StructType::get(*context, type_elems));
    return builder->CreateBitCast(val, type);
}

extern "C" Value *codegen_tagged_cast(Value *val, u64 n) {
    vector<Type *> type_elems = {tagged_header_type};
    for (u64 i = 0; i < n; i++)
        type_elems.push_back(box_type);
    Type *type = PointerType::getUnqual(StructType::get(*context, type_elems));
    return builder->CreateBitCast(val, type);
}

extern "C" PatternEnd *codegen_tag_check(Value *val, u64 tag) {
    Value *val_tag = builder->CreateLoad(cg_gep(val, {builder->getInt32(0), builder->getInt32(1)}));
    Value *tag_test = builder->CreateICmpEQ(val_tag, builder->getInt64(tag));
    BasicBlock *parent_block = builder->GetInsertBlock();
    BasicBlock *success_block = BasicBlock::Create(*context, "", parent_block->getParent());
    builder->SetInsertPoint(success_block);
    return new PatternEnd {parent_block, tag_test, success_block};
}

extern "C" Value *codegen_tuple_get(Value *val, u64 i) {
    return builder->CreateLoad(builder->CreateStructGEP(val, 1 + i));
}

extern "C" Value *codegen_list_cast(Value *val) {
    return builder->CreateBitCast(val, PointerType::getUnqual(list_type));
}

extern "C" PatternEnd *codegen_len_check(Value *val, u64 len) {
    Value *val_len = builder->CreateLoad(builder->CreateStructGEP(val, 1));
    Value *len_test = builder->CreateICmpEQ(val_len, builder->getInt64(len));
    BasicBlock *parent_block = builder->GetInsertBlock();
    BasicBlock *success_block = BasicBlock::Create(*context, "", parent_block->getParent());
    builder->SetInsertPoint(success_block);
    return new PatternEnd {parent_block, len_test, success_block};
}

extern "C" Value *codegen_list_get(Value *val, u64 i) {
    return builder->CreateLoad(cg_gep(val, {builder->getInt32(3), builder->getInt64(i)}));
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
            ret_int ? Type::getInt64Ty(*context) : Type::getVoidTy(*context),
            vector<Type *>(params, Type::getInt64Ty(*context)),
            false),
        Function::ExternalLinkage, name, module_);
    BasicBlock *saved_bb = builder->GetInsertBlock();
    Function *func = Function::Create(get_function_type(params), Function::PrivateLinkage, "", module_);
    builder->SetInsertPoint(BasicBlock::Create(*context, "", func));
    vector<Value *> extern_func_args;
    for (u64 i = 0; i < params; i++) {
        Value *arg = builder->CreateBitCast(func->getArg(i), PointerType::getUnqual(StructType::get(*context, {box_header_type, Type::getInt64Ty(*context)})));
        extern_func_args.push_back(builder->CreateLoad(builder->CreateStructGEP(arg, 1)));
    }
    Value *extern_func_ret = builder->CreateCall(extern_func->getFunctionType(), extern_func, extern_func_args);
    if (ret_int) {
        Value *ret = codegen_malloc(StructType::get(*context, {box_header_type, Type::getInt64Ty(*context)}));
        codegen_box_header_init(ret, base_dtor);
        builder->CreateStore(extern_func_ret, builder->CreateStructGEP(ret, 1));
        builder->CreateRet(builder->CreateBitCast(ret, box_type));
    } else {
        builder->CreateRet(codegen_tuple_({}, -1));
    }
    builder->SetInsertPoint(saved_bb);
    return codegen_boxed_fuction(func);
}

extern "C" void llvm_fin() {
    builder->CreateRetVoid();
    module_->print(outs(), nullptr);
    if (verifyModule(*module_, &errs()) == true) {
        exit(1);
    }
}
