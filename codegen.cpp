#include "global.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
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
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include <iostream>
#include <filesystem>

#include "program_main.h"

using namespace llvm;

static LLVMContext *context;
static IRBuilder<> *builder;
static Module *module_;
static TargetMachine *target_machine;

static StructType *captures_header_type;
static FunctionType *captures_dtor_type;
static FunctionType *boxed_dtor_type;
static Function *malloc_func;
static Function *realloc_func;
static Function *free_func;
static Function *overflow_error_func;
static Function *div_by_zero_error_func;
static Function *bounds_error_func;
static Function *pop_bounds_error_func;
static Function *unreachable_error_func;

static StructType *struct_type(ArrayRef<Type *> elems) {
    return StructType::get(*context, elems);
}

static Value *cg_call(FunctionType *type, Value *func, ArrayRef<Value *> args) {
    return builder->CreateCall(type, func, args);
}

static Value *cg_call(Function *func, ArrayRef<Value *> args) {
    return builder->CreateCall(func->getFunctionType(), func, args);
}

extern "C" Value *cg_call_(Function *func, vector<Value *> *args) {
    return builder->CreateCall(func->getFunctionType(), func, *args);
}

extern "C" Type *pointer_type(Type *type) {
    return PointerType::getUnqual(type);
}

static Type *i64_type;

extern "C" Type *get_i64_type() {
    return i64_type;
}

extern "C" ConstantInt *cg_i32(u64 n) {
    return builder->getInt32(n);
}

extern "C" ConstantInt *cg_i64(u64 n) {
    return builder->getInt64(n);
}

extern "C" Value *cg_bitcast(Value *val, Type *type) {
    return builder->CreateBitCast(val, type);
}

extern "C" Value *cg_sgep(Value *val, u64 n) {
    return builder->CreateStructGEP(val, n);
}

extern "C" Value *cg_load(Value *ptr) {
    return builder->CreateLoad(ptr);
}

extern "C" void cg_store(Value *val, Value *ptr) {
    builder->CreateStore(val, ptr);
}

extern "C" void cg_br(BasicBlock *bb) {
    builder->CreateBr(bb);
}

extern "C" void cg_cond_br(Value *val, BasicBlock *t, BasicBlock *f) {
    builder->CreateCondBr(val, t, f);
}

extern "C" void cg_unreachable() {
    builder->CreateUnreachable();
}

extern "C" PHINode *cg_phi(Type *type, u64 reserved) {
    return builder->CreatePHI(type, reserved);
}

extern "C" void cg_ret(Value *val) {
    builder->CreateRet(val);
}

extern "C" void cg_ret_void() {
    builder->CreateRetVoid();
}

#define cg_insert_value builder->CreateInsertValue
#define cg_extract_value builder->CreateExtractValue

extern "C" Value *cg_and(Value *v1, Value *v2) {
    return builder->CreateAnd(v1, v2);
}

extern "C" Value *cg_add(Value *v1, Value *v2) {
    return builder->CreateAdd(v1, v2);
}

extern "C" Value *cg_sub(Value *v1, Value *v2) {
    return builder->CreateSub(v1, v2);
}

extern "C" Value *cg_mul(Value *v1, Value *v2) {
    return builder->CreateMul(v1, v2);
}

extern "C" Value *cg_icmp_eq(Value *v1, Value *v2) {
    return builder->CreateICmpEQ(v1, v2);
}

extern "C" Value *cg_icmp_ne(Value *v1, Value *v2) {
    return builder->CreateICmpNE(v1, v2);
}

extern "C" Value *cg_icmp_sge(Value *v1, Value *v2) {
    return builder->CreateICmpSGE(v1, v2);
}

extern "C" Value *cg_icmp_ult(Value *v1, Value *v2) {
    return builder->CreateICmpULT(v1, v2);
}

extern "C" Value *cg_icmp_uge(Value *v1, Value *v2) {
    return builder->CreateICmpUGE(v1, v2);
}

extern "C" Value *cg_poison(Type *type) {
    return PoisonValue::get(type);
}

static Value *cg_gep(Value *val, vector<Value *> idx_) {
    vector<Value *> idx = {cg_i64(0)};
    for (Value *v : idx_)
        idx.push_back(v);
    return builder->CreateInBoundsGEP(val, idx);
}

extern "C" Value *cg_gep(Value *val, vector<Value *> *idx_) {
    return cg_gep(val, *idx_);
}

extern "C" BasicBlock *create_basic_block(Function *func) {
    return BasicBlock::Create(*context, "", func);
}

extern "C" Function *get_basic_block_parent(BasicBlock *bb) {
    return bb->getParent();
}

extern "C" Function *create_function(FunctionType *ftype) {
    return Function::Create(ftype, Function::PrivateLinkage, "", module_);
}

extern "C" BasicBlock *get_insert_block() {
    return builder->GetInsertBlock();
}

extern "C" void set_insert_block(BasicBlock *bb) {
    builder->SetInsertPoint(bb);
}

extern "C" BasicBlock *create_block() {
    return create_basic_block(get_insert_block()->getParent());
}

extern "C" FunctionType *get_function_type(vector<Type *> *param_types, Type *ret_type) {
    param_types->push_back(pointer_type(captures_header_type));
    FunctionType *func_type = FunctionType::get(ret_type, *param_types, false);
    param_types->pop_back();
    return func_type;
}

extern "C" Function *codegen_new_function(vector<Type *> *param_types, Type *ret_type) {
    return create_function(get_function_type(param_types, ret_type));
}

extern "C" Function *begin_captures_dtor() {
    Function *dtor = create_function(captures_dtor_type);
    set_insert_block(create_basic_block(dtor));
    return dtor;
}

extern "C" Function *create_boxed_dtor() {
    return create_function(boxed_dtor_type);
}

extern "C" void codegen_end_dtor(Value *val) {
    cg_call(free_func, {cg_bitcast(val, Type::getInt8PtrTy(*context))});
    cg_ret_void();
}

extern "C" Value *codegen_cell_rc_decr_get_rc(Value *cell) {
    return cg_load(cg_sgep(cell, 0));
}

extern "C" void codegen_cell_rc_decr_decr_rc(Value *rc, Value *cell) {
    cg_store(cg_sub(rc, cg_i64(1)), cg_sgep(cell, 0));
}

extern "C" void codegen_cell_rc_decr_free(Value *cell) {
    cg_call(free_func, {cg_bitcast(cell, Type::getInt8PtrTy(*context))});
}

extern "C" void codegen_cell_rc_decr_branch(Value *rc, BasicBlock *free_block, BasicBlock *decr_block) {
    cg_cond_br(cg_icmp_eq(rc, cg_i64(0)), free_block, decr_block);
}

extern "C" void codegen_cell_rc_incr(Value *cell) {
    Value *rc_ptr = cg_sgep(cell, 0);
    Value *rc = cg_load(rc_ptr);
    cg_store(cg_add(rc, cg_i64(1)), rc_ptr);
}

extern "C" Type *cell_type(Type *type) {
    return pointer_type(struct_type({i64_type, type}));
}

extern "C" Type *list_type(Type *elem_type) {
    return struct_type({i64_type, i64_type, i64_type, ArrayType::get(elem_type, 0)});
}

extern "C" Value *codegen_list_dtor_get_val(Type *elem_type) {
    return cg_bitcast(get_insert_block()->getParent()->getArg(0), pointer_type(list_type(elem_type)));
}

extern "C" Value *codegen_list_dtor_extract_len(Value *val) {
    return cg_load(cg_sgep(val, 1));
}

extern "C" Value *codegen_list_dtor_extract_elem(Value *val, Value *i) {
    return cg_load(cg_gep(val, {cg_i32(3), i}));
}

extern "C" Value *codegen_list_dtor_add_1(Value *i) {
    return cg_add(i, cg_i64(1));
}

extern "C" void codegen_list_dtor_branch(Value *i, Value *len, BasicBlock *loop_block, BasicBlock *end_block) {
    cg_cond_br(cg_icmp_ult(i, len), loop_block, end_block);
}

extern "C" Type *codegen_int_type() {
    return i64_type;
}

extern "C" Type *codegen_boxed_type() {
    return pointer_type(i64_type);
}

extern "C" Type *codegen_function_type(vector<Type *> *params, Type *ret) {
    return struct_type({pointer_type(get_function_type(params, ret)), pointer_type(captures_header_type)});
}

static FunctionType *error_func_type;

extern "C" Value *codegen_error_func(vector<u64> *name_vector) {
    string name;
    for (u64 c : *name_vector)
        name.push_back(c);
    return Function::Create(error_func_type, Function::ExternalLinkage, name, module_);
}

extern "C" Value *get_error_func(u64 i) {
    vector<Value *> funcs = {overflow_error_func, div_by_zero_error_func, bounds_error_func, pop_bounds_error_func, unreachable_error_func};
    return funcs[i];
}

extern "C" void llvm_init(u64 opt_level) {
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
    target_machine = target->createTargetMachine(target_triple, "generic", "", TargetOptions(), Optional<Reloc::Model>(Reloc::PIC_), None, (CodeGenOpt::Level)opt_level);
    DataLayout data_layout = target_machine->createDataLayout();
    context = new LLVMContext();
    SMDiagnostic err;
    module_ = new Module("", *context);
    builder = new IRBuilder<>(*context);
    module_->setDataLayout(data_layout);
    module_->setTargetTriple(target_triple);
    i64_type = Type::getInt64Ty(*context);
    captures_header_type = StructType::create(*context, "Captures");
    captures_dtor_type = FunctionType::get(Type::getVoidTy(*context), {pointer_type(captures_header_type)}, false);
    captures_header_type->setBody({Type::getInt64Ty(*context), pointer_type(captures_dtor_type)});
    boxed_dtor_type = FunctionType::get(Type::getVoidTy(*context), {pointer_type(i64_type)}, false);
    malloc_func = Function::Create(FunctionType::get(Type::getInt8PtrTy(*context), {Type::getInt64Ty(*context)}, false), Function::ExternalLinkage, "malloc", module_);
    realloc_func = Function::Create(FunctionType::get(Type::getInt8PtrTy(*context), {Type::getInt8PtrTy(*context), Type::getInt64Ty(*context)}, false), Function::ExternalLinkage, "realloc", module_);
    free_func = Function::Create(FunctionType::get(Type::getVoidTy(*context), {Type::getInt8PtrTy(*context)}, false), Function::ExternalLinkage, "free", module_);
    error_func_type = FunctionType::get(Type::getVoidTy(*context), false);
    overflow_error_func = Function::Create(error_func_type, Function::ExternalLinkage, "overflow_error", module_);
    div_by_zero_error_func = Function::Create(error_func_type, Function::ExternalLinkage, "div_by_zero_error", module_);
    bounds_error_func = Function::Create(error_func_type, Function::ExternalLinkage, "bounds_error", module_);
    pop_bounds_error_func = Function::Create(error_func_type, Function::ExternalLinkage, "pop_bounds_error", module_);
    unreachable_error_func = Function::Create(error_func_type, Function::ExternalLinkage, "unreachable_error", module_);
    Function *main = Function::Create(FunctionType::get(Type::getVoidTy(*context), false), Function::ExternalLinkage, "hagane_main", module_);
    set_insert_block(create_basic_block(main));
}

static bool is_type_small(Type *type) {
    return module_->getDataLayout().getTypeAllocSize(type) <= 16;
}

static Value *codegen_malloc(Value *size, Type *type) {
    Value *val = cg_call(malloc_func, {size});
    return cg_bitcast(val, pointer_type(type));
}

extern "C" Value *codegen_malloc(Type *type) {
    Value *val = cg_call(malloc_func, {ConstantExpr::getSizeOf(type)});
    return cg_bitcast(val, pointer_type(type));
}

static Value *codegen_realloc(Value *old, Value *size, Type *type) {
    Value *val = cg_call(realloc_func, {cg_bitcast(old, Type::getInt8PtrTy(*context)), size});
    return cg_bitcast(val, pointer_type(type));
}

extern "C" void codegen_rc_init(Value *val) {
    cg_store(cg_i64(0), val);
}

extern "C" Value *codegen_const_int(u64 value) {
    return cg_i64(value);
}

extern "C" Value *codegen_malloc_empty_list() {
    StructType *list_type = struct_type({i64_type, i64_type, i64_type});
    return codegen_malloc(ConstantExpr::getSizeOf(list_type), list_type);
}

extern "C" Value *codegen_malloc_list(Value *size, Type *elem_type) {
    Type *list_type_ = list_type(elem_type);
    Value *data_size = cg_mul(ConstantExpr::getSizeOf(elem_type), size);
    return codegen_malloc(cg_add(ConstantExpr::getSizeOf(list_type_), data_size), list_type_);
}

extern "C" Value *codegen_realloc_list(Value *old, Value *size, Type *elem_type) {
    Type *list_type_ = list_type(elem_type);
    Value *data_size = cg_mul(ConstantExpr::getSizeOf(elem_type), size);
    return codegen_realloc(old, cg_add(ConstantExpr::getSizeOf(list_type_), data_size), list_type_);
}

extern "C" Value *codegen_list(vector<Value *> *elems, Type *type) {
    u64 cap = elems->size() > 0 ? elems->size() : 1;
    Value *val = codegen_malloc_list(cg_i64(cap), type);
    codegen_rc_init(cg_sgep(val, 0));
    cg_store(cg_i64(elems->size()), cg_sgep(val, 1));
    cg_store(cg_i64(cap), cg_sgep(val, 2));
    for (size_t i = 0; i < elems->size(); i++)
        cg_store((*elems)[i], cg_gep(val, {cg_i32(3), cg_i64(i)}));
    return cg_bitcast(val, pointer_type(i64_type));
}

extern "C" Value *codegen_create_mut_var(Value *val) {
    Type *cell_type = struct_type({i64_type, val->getType()});
    Value *ptr = codegen_malloc(cell_type);
    codegen_rc_init(cg_sgep(ptr, 0));
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
    cg_unreachable();
}

extern "C" void codegen_br(BasicBlock *bb) {
    cg_br(bb);
}

extern "C" void codegen_cond_br(Value *cond, BasicBlock *then, BasicBlock *else_) {
    Type *tagged_box_type = pointer_type(struct_type({i64_type, i64_type}));
    cg_cond_br(cg_icmp_ne(cg_load(cg_sgep(cg_bitcast(cond, tagged_box_type), 1)), cg_i64(0)), then, else_);
}

extern "C" PHINode *codegen_phi(Type *type, u64 reserved) {
    return cg_phi(type, reserved);
}

extern "C" void phi_add_incoming(PHINode *phi, Value *val, BasicBlock *bb) {
    phi->addIncoming(val, bb);
}

extern "C" Value *codegen_apply(Value *func_val, vector<Value *> *args) {
    Value *func = cg_extract_value(func_val, {0});
    Value *captures = cg_extract_value(func_val, {1});
    args->push_back(captures);
    Value *val = cg_call((FunctionType *)(func->getType()->getPointerElementType()), func, *args);
    args->pop_back();
    return val;
}

extern "C" Value *cg_get_arg(u64 i) {
    return get_insert_block()->getParent()->getArg(i);
}

extern "C" Value *codegen_get_arg(u64 i) {
    return get_insert_block()->getParent()->getArg(i);
}

extern "C" Value *codegen_get_captures(Value *outer) {
    Function *func = get_insert_block()->getParent();
    return func->getArg(func->arg_size() - 1);
}

extern "C" Type *codegen_type_of(Value *val) {
    return val->getType();
}

extern "C" Type *codegen_pointer_type(Type *type) {
    return pointer_type(type);
}

extern "C" Value *codegen_bitcast(Value *val, Type *type) {
    return cg_bitcast(val, type);
}

extern "C" Value *codegen_extract_capture(Value *captures, u64 i) {
    return cg_load(cg_sgep(captures, 1 + i));
}

extern "C" void codegen_ret(Value *val) {
    cg_ret(val);
}

extern "C" Value *codegen_extract_captures(Value *func) {
    return cg_extract_value(func, {1});
}

extern "C" void codegen_captures_rc_incr(Value *captures) {
    captures = cg_bitcast(captures, pointer_type(captures_header_type));
    Value *rc_ptr = cg_sgep(captures, 0);
    Value *rc = cg_load(rc_ptr);
    cg_store(cg_add(rc, cg_i64(1)), rc_ptr);
}

extern "C" void codegen_captures_rc_decr(Value *captures) {
    captures = cg_bitcast(captures, pointer_type(captures_header_type));
    BasicBlock *free_block = create_block();
    BasicBlock *decr_block = create_block();
    BasicBlock *end_block = create_block();

    Value *rc_ptr = cg_sgep(captures, 0);
    Value *rc = cg_load(rc_ptr);
    cg_cond_br(cg_icmp_eq(rc, cg_i64(0)), free_block, decr_block);

    set_insert_block(free_block);
    cg_call(captures_dtor_type, cg_load(cg_sgep(captures, 1)), {captures});
    cg_br(end_block);

    set_insert_block(decr_block);
    cg_store(cg_sub(rc, cg_i64(1)), rc_ptr);
    cg_br(end_block);

    set_insert_block(end_block);
}

extern "C" Type *codegen_captures_type(vector<Value *> *vals) {
    vector<Type *> types = {captures_header_type};
    for (Value *val : *vals)
        types.push_back(val->getType());
    return struct_type(types);
}

extern "C" Value *codegen_captures_from_list(Type *type, vector<Value *> *vals, Value *dtor) {
    Value *captures = codegen_malloc(type);
    codegen_rc_init(cg_gep(captures, {cg_i32(0), cg_i32(0)}));
    cg_store(dtor, cg_gep(captures, {cg_i32(0), cg_i32(1)}));
    for (size_t i = 0; i < vals->size(); i++)
        cg_store((*vals)[i], cg_sgep(captures, 1 + i));
    return captures;
}

extern "C" Value *codegen_empty_captures() {
    BasicBlock *parent_block = get_insert_block();
    Function *dtor = begin_captures_dtor();
    codegen_end_dtor(codegen_get_arg(0));
    set_insert_block(parent_block);
    vector <Value *> vals;
    return codegen_captures_from_list(codegen_captures_type(&vals), &vals, dtor);
}

extern "C" Value *codegen_func_val(Function *func, Value *captures) {
    Type *type = struct_type({func->getType(), pointer_type(captures_header_type)});
    Value *val = cg_poison(type);
    val = cg_insert_value(val, func, {0});
    val = cg_insert_value(val, cg_bitcast(captures, pointer_type(captures_header_type)), {1});
    return val;
}

static Value *codegen_tagless_boxed_tuple(vector<Value *> *elems) {
    vector<Type *> type_elems = {i64_type};
    for (unsigned i = 0; i < elems->size(); i++)
        type_elems.push_back((*elems)[i]->getType());
    Type *type = struct_type(type_elems);
    Value *val = codegen_malloc(type);
    codegen_rc_init(cg_sgep(val, 0));
    for (unsigned i = 0; i < elems->size(); i++)
        cg_store((*elems)[i], cg_sgep(val, 1 + i));
    return cg_bitcast(val, pointer_type(i64_type));
}

static Value *codegen_tagged_boxed_tuple(vector<Value *> *elems, i64 tag) {
    vector<Type *> type_elems = {struct_type({i64_type, i64_type})};
    for (unsigned i = 0; i < elems->size(); i++)
        type_elems.push_back((*elems)[i]->getType());
    Type *type = struct_type(type_elems);
    Value *val = codegen_malloc(type);
    codegen_rc_init(cg_gep(val, {cg_i32(0), cg_i32(0)}));
    cg_store(cg_i64(tag), cg_gep(val, {cg_i32(0), cg_i32(1)}));
    for (unsigned i = 0; i < elems->size(); i++)
        cg_store((*elems)[i], cg_sgep(val, 1 + i));
    return cg_bitcast(val, pointer_type(i64_type));
}

extern "C" Value *codegen_create_tagless_boxed_ctor(vector<Type *> *param_types) {
    if (param_types->size() > 0) {
        BasicBlock *saved_bb = get_insert_block();
        Function *ctor = codegen_new_function(param_types, pointer_type(i64_type));
        set_insert_block(create_basic_block(ctor));
        vector<Value *> elems;
        for (u64 i = 0; i < param_types->size(); i++)
            elems.push_back(ctor->getArg(i));
        cg_ret(codegen_tagless_boxed_tuple(&elems));
        set_insert_block(saved_bb);
        return codegen_func_val(ctor, codegen_empty_captures());
    } else {
        vector<Value *> elems;
        return codegen_tagless_boxed_tuple(&elems);
    }
}

extern "C" Value *codegen_create_tagged_boxed_ctor(vector<Type *> *param_types, u64 tag) {
    if (param_types->size() > 0) {
        BasicBlock *saved_bb = get_insert_block();
        Function *ctor = codegen_new_function(param_types, pointer_type(i64_type));
        set_insert_block(create_basic_block(ctor));
        vector<Value *> elems;
        for (u64 i = 0; i < param_types->size(); i++)
            elems.push_back(ctor->getArg(i));
        cg_ret(codegen_tagged_boxed_tuple(&elems, tag));
        set_insert_block(saved_bb);
        return codegen_func_val(ctor, codegen_empty_captures());
    } else {
        vector<Value *> elems;
        return codegen_tagged_boxed_tuple(&elems, tag);
    }
}

extern "C" Value *codegen_tagless_boxed_cast(Value *val, vector<Type *> *types) {
    vector<Type *> type_components = {i64_type};
    for (Type *type : *types)
        type_components.push_back(type);
    Type *type = struct_type(type_components);
    return cg_bitcast(val, pointer_type(type));
}

extern "C" Value *codegen_tagged_boxed_cast(Value *val, vector<Type *> *types) {
    vector<Type *> type_components = {struct_type({i64_type, i64_type})};
    for (Type *type : *types)
        type_components.push_back(type);
    Type *type = struct_type(type_components);
    return cg_bitcast(val, pointer_type(type));
}

extern "C" Value *codegen_boxed_extract(Value *val, u64 i) {
    return cg_load(cg_sgep(val, i + 1));
}

extern "C" SwitchInst *codegen_tagged_boxed_dtor_branch(Value *val, u64 num_variants) {
    BasicBlock *parent_block = get_insert_block();
    Value *tag = cg_load(cg_sgep(cg_bitcast(val, pointer_type(struct_type({i64_type, i64_type}))), 1));
    BasicBlock *undef_block = create_block();
    set_insert_block(undef_block);
    cg_unreachable();
    set_insert_block(parent_block);
    SwitchInst *switch_inst = builder->CreateSwitch(tag, undef_block, num_variants);
    return switch_inst;
}

extern "C" void switch_add_case(SwitchInst *switch_inst, u64 i, BasicBlock *block) {
    switch_inst->addCase(cg_i64(i), block);
}

extern "C" void codegen_boxed_rc_incr(Value *val) {
    Value *rc = cg_load(val);
    cg_store(cg_add(rc, cg_i64(1)), val);
}

extern "C" void codegen_boxed_rc_decr(Value *val, Function *dtor) {
    BasicBlock *free_block = create_block();
    BasicBlock *decr_block = create_block();
    BasicBlock *end_block = create_block();

    Value *rc = cg_load(val);
    cg_cond_br(cg_icmp_eq(rc, cg_i64(0)), free_block, decr_block);

    set_insert_block(free_block);
    cg_call(boxed_dtor_type, dtor, {val});
    cg_br(end_block);

    set_insert_block(decr_block);
    cg_store(cg_sub(rc, cg_i64(1)), val);
    cg_br(end_block);

    set_insert_block(end_block);
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
    Value *eq_test = cg_icmp_eq(val, cg_i64(n));
    BasicBlock *parent_block = get_insert_block();
    BasicBlock *success_block = create_basic_block(parent_block->getParent());
    set_insert_block(success_block);
    return new PatternEnd {parent_block, eq_test, success_block};
}

extern "C" PatternEnd *codegen_boxed_tag_check(Value *val, u64 tag) {
    Value *val_tag = cg_load(cg_gep(val, {cg_i32(0), cg_i32(1)}));
    Value *tag_test = cg_icmp_eq(val_tag, cg_i64(tag));
    BasicBlock *parent_block = get_insert_block();
    BasicBlock *success_block = create_basic_block(parent_block->getParent());
    set_insert_block(success_block);
    return new PatternEnd {parent_block, tag_test, success_block};
}

extern "C" Value *codegen_list_cast(Value *val, Type *elem_type) {
    return cg_bitcast(val, pointer_type(list_type(elem_type)));
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
    vector<Type *> func_params(params, i64_type);
    Function *func = create_function(get_function_type(&func_params, ret_int ? (Type *)i64_type : (Type *)pointer_type(i64_type)));
    set_insert_block(create_basic_block(func));
    vector<Value *> extern_func_args;
    for (u64 i = 0; i < params; i++)
        extern_func_args.push_back(func->getArg(i));
    Value *extern_func_ret = cg_call(extern_func, extern_func_args);
    if (ret_int) {
        cg_ret(extern_func_ret);
    } else {
        vector <Value *> elems;
        cg_ret(codegen_tagless_boxed_tuple(&elems));
    }
    set_insert_block(saved_bb);
    return codegen_func_val(func, codegen_empty_captures());
}

static Intrinsic::ID arith_intrinsics[3] = {
    Intrinsic::sadd_with_overflow,
    Intrinsic::ssub_with_overflow,
    Intrinsic::smul_with_overflow,
};

extern "C" Value *codegen_arith_primitive(u64 op) {
    BasicBlock *saved_bb = builder->GetInsertBlock();
    vector<Type *> param_types = {i64_type, i64_type};
    Function *func = create_function(get_function_type(&param_types, i64_type));
    BasicBlock *entry_block = create_basic_block(func);
    BasicBlock *overflow_block = create_basic_block(func);
    BasicBlock *exit_block = create_basic_block(func);

    set_insert_block(entry_block);
    Value *a = func->getArg(0);
    Value *b = func->getArg(1);
    Value *r = builder->CreateBinaryIntrinsic(arith_intrinsics[op], a, b);
    cg_cond_br(cg_extract_value(r, {1}), overflow_block, exit_block);

    set_insert_block(overflow_block);
    cg_call(overflow_error_func, {});
    cg_unreachable();

    set_insert_block(exit_block);
    cg_ret(cg_extract_value(r, {0}));

    set_insert_block(saved_bb);
    return codegen_func_val(func, codegen_empty_captures());
}

extern "C" Value *codegen_div_primitive(u64 is_mod) {
    BasicBlock *saved_bb = builder->GetInsertBlock();
    vector<Type *> param_types = {i64_type, i64_type};
    Function *func = create_function(get_function_type(&param_types, i64_type));
    BasicBlock *entry_block = create_basic_block(func);
    BasicBlock *div_by_zero_block = create_basic_block(func);
    BasicBlock *overflow_check_block = create_basic_block(func);
    BasicBlock *overflow_block = create_basic_block(func);
    BasicBlock *d1_block = create_basic_block(func);
    BasicBlock *d2_block = create_basic_block(func);
    BasicBlock *d3_block = create_basic_block(func);
    BasicBlock *d4_block = create_basic_block(func);
    BasicBlock *d5_block = create_basic_block(func);
    BasicBlock *d6_block = create_basic_block(func);
    BasicBlock *exit_block = create_basic_block(func);

    set_insert_block(entry_block);
    Value *a = func->getArg(0);
    Value *b = func->getArg(1);
    cg_cond_br(cg_icmp_eq(b, cg_i64(0)), div_by_zero_block, overflow_check_block);

    set_insert_block(div_by_zero_block);
    cg_call(div_by_zero_error_func, {});
    cg_unreachable();

    set_insert_block(overflow_check_block);
    Value *av = cg_icmp_eq(a, cg_i64(INT64_MIN));
    Value *bv = cg_icmp_eq(b, cg_i64(-1));
    cg_cond_br(cg_and(av, bv), overflow_block, d1_block);

    set_insert_block(overflow_block);
    if (is_mod) {
        cg_ret(cg_i64(0));
    } else {
        cg_call(overflow_error_func, {});
        cg_unreachable();
    }

    set_insert_block(d1_block);
    cg_cond_br(cg_icmp_sge(a, cg_i64(0)), d2_block, d3_block);
    set_insert_block(d2_block);
    cg_cond_br(cg_icmp_sge(b, cg_i64(0)), d4_block, d5_block);
    set_insert_block(d3_block);
    cg_cond_br(cg_icmp_sge(b, cg_i64(0)), d6_block, d4_block);
    set_insert_block(d4_block);
    cg_br(exit_block);
    set_insert_block(d5_block);
    Value *bp1 = cg_add(b, cg_i64(1));
    cg_br(exit_block);
    set_insert_block(d6_block);
    Value *bm1 = cg_sub(b, cg_i64(1));
    cg_br(exit_block);

    set_insert_block(exit_block);
    PHINode *d = cg_phi(i64_type, 3);
    d->addIncoming(cg_i64(0), d4_block);
    d->addIncoming(bp1, d5_block);
    d->addIncoming(bm1, d6_block);
    Value *a_ = cg_sub(a, d);
    Value *r = is_mod ? builder->CreateSRem(a_, b) : builder->CreateSDiv(a_, b);
    Value *r_ = is_mod ? cg_add(r, d) : r;
    cg_ret(r_);
    set_insert_block(saved_bb);
    return codegen_func_val(func, codegen_empty_captures());
}

static CmpInst::Predicate cmp_predicates[6] = {
    CmpInst::ICMP_EQ,
    CmpInst::ICMP_NE,
    CmpInst::ICMP_SLT,
    CmpInst::ICMP_SLE,
    CmpInst::ICMP_SGT,
    CmpInst::ICMP_SGE,
};

static Value *codegen_bool(Value *from) {
    Type *type = struct_type({i64_type, i64_type});
    Value *val = codegen_malloc(type);
    codegen_rc_init(cg_sgep(val, 0));
    cg_store(builder->CreateZExt(from, i64_type), cg_sgep(val, 1));
    return cg_bitcast(val, pointer_type(i64_type));
}

extern "C" Value *codegen_cmp_primitive(u64 pred) {
    BasicBlock *saved_bb = builder->GetInsertBlock();
    vector<Type *> param_types = {i64_type, i64_type};
    Function *func = create_function(get_function_type(&param_types, pointer_type(i64_type)));
    set_insert_block(create_basic_block(func));
    Value *a = func->getArg(0);
    Value *b = func->getArg(1);
    cg_ret(codegen_bool(builder->CreateZExt(builder->CreateICmp(cmp_predicates[pred], a, b), i64_type)));
    set_insert_block(saved_bb);
    return codegen_func_val(func, codegen_empty_captures());
}

extern "C" Value *codegen_len_primitive(Type *elem_type, Function *dtor) {
    BasicBlock *saved_bb = builder->GetInsertBlock();
    vector <Type *> param_types = {pointer_type(i64_type)};
    Function *func = create_function(get_function_type(&param_types, i64_type));
    set_insert_block(create_basic_block(func));
    Value *l = cg_bitcast(func->getArg(0), pointer_type(list_type(elem_type)));
    Value *r = cg_load(cg_sgep(l, 1));
    codegen_boxed_rc_decr(func->getArg(0), dtor);
    cg_ret(r);
    set_insert_block(saved_bb);
    return codegen_func_val(func, codegen_empty_captures());
}

static void codegen_rc_incr(Value *val, u64 ref_type) {
    switch (ref_type) {
    case 0:
        break;
    case 1:
        codegen_boxed_rc_incr(val);
        break;
    case 2:
        codegen_captures_rc_incr(codegen_extract_captures(val));
        break;
    default:
        cerr << "Internal error: invalid ref_type\n";
        exit(1);
    }
}

extern "C" Value *codegen_get_primitive(Type *elem_type, Function *dtor, u64 ref_type) {
    BasicBlock *saved_bb = builder->GetInsertBlock();
    vector <Type *> param_types = {pointer_type(i64_type), i64_type};
    Function *func = create_function(get_function_type(&param_types, elem_type));
    BasicBlock *entry_block = create_basic_block(func);
    BasicBlock *out_of_range_block = create_basic_block(func);
    BasicBlock *exit_block = create_basic_block(func);

    set_insert_block(entry_block);
    Value *l = cg_bitcast(func->getArg(0), pointer_type(list_type(elem_type)));
    Value *i = func->getArg(1);
    Value *len = cg_load(cg_sgep(l, 1));
    cg_cond_br(cg_icmp_uge(i, len), out_of_range_block, exit_block);

    set_insert_block(out_of_range_block);
    codegen_boxed_rc_decr(func->getArg(0), dtor);
    cg_call(bounds_error_func, {});
    cg_unreachable();

    set_insert_block(exit_block);
    Value *r = cg_load(cg_gep(l, {cg_i32(3), i}));
    codegen_rc_incr(r, ref_type);
    codegen_boxed_rc_decr(func->getArg(0), dtor);
    cg_ret(r);
    set_insert_block(saved_bb);
    return codegen_func_val(func, codegen_empty_captures());
}

static void codegen_copy_loop(Value *len, Value *src, Value *dest, u64 ref_type) {
    BasicBlock *parent_block = get_insert_block();
    BasicBlock *copy_check_block = create_basic_block(parent_block->getParent());
    BasicBlock *copy_loop_block = create_basic_block(parent_block->getParent());
    BasicBlock *exit_block = create_basic_block(parent_block->getParent());
    cg_br(copy_check_block);

    set_insert_block(copy_check_block);
    PHINode *ci = cg_phi(i64_type, 2);
    ci->addIncoming(cg_i64(0), parent_block);
    cg_cond_br(cg_icmp_ult(ci, len), copy_loop_block, exit_block);

    set_insert_block(copy_loop_block);
    Value *e = cg_load(cg_gep(src, {ci}));
    codegen_rc_incr(e, ref_type);
    cg_store(e, cg_gep(dest, {ci}));
    ci->addIncoming(cg_add(ci, cg_i64(1)), copy_loop_block);
    cg_br(copy_check_block);

    set_insert_block(exit_block);
}

extern "C" Value *codegen_pop_primitive(Type *elem_type, Function *dtor, u64 ref_type) {
    BasicBlock *saved_bb = builder->GetInsertBlock();
    vector <Type *> param_types = {pointer_type(i64_type)};
    Function *func = create_function(get_function_type(&param_types, pointer_type(i64_type)));
    BasicBlock *entry_block = create_basic_block(func);
    BasicBlock *out_of_range_block = create_basic_block(func);
    BasicBlock *alloc_block = create_basic_block(func);

    set_insert_block(entry_block);
    Value *l = cg_bitcast(func->getArg(0), pointer_type(list_type(elem_type)));
    Value *len = cg_load(cg_sgep(l, 1));
    cg_cond_br(cg_icmp_eq(len, cg_i64(0)), out_of_range_block, alloc_block);

    set_insert_block(out_of_range_block);
    codegen_boxed_rc_decr(func->getArg(0), dtor);
    cg_call(pop_bounds_error_func, {});
    cg_unreachable();

    set_insert_block(alloc_block);
    Value *new_len = cg_sub(len, cg_i64(1));
    Value *val = codegen_malloc_list(new_len, elem_type);
    codegen_rc_init(cg_sgep(val, 0));
    cg_store(new_len, cg_sgep(val, 1));
    cg_store(new_len, cg_sgep(val, 2));
    codegen_copy_loop(new_len, cg_sgep(l, 3), cg_sgep(val, 3), ref_type);
    codegen_boxed_rc_decr(func->getArg(0), dtor);
    cg_ret(cg_bitcast(val, pointer_type(i64_type)));
    set_insert_block(saved_bb);
    return codegen_func_val(func, codegen_empty_captures());
}

extern "C" Value *codegen_unreachable_primitive(Type *ret_type) {
    BasicBlock *saved_bb = builder->GetInsertBlock();
    vector <Type *> param_types = {};
    Function *func = create_function(get_function_type(&param_types, ret_type));
    set_insert_block(create_basic_block(func));
    cg_call(unreachable_error_func, {});
    cg_unreachable();
    set_insert_block(saved_bb);
    return codegen_func_val(func, codegen_empty_captures());
}

extern "C" void print_type_of(Value *val) {
    val->getType()->print(outs());
    cout << "\n";
}

static char temp_dir_path_buffer[L_tmpnam];

extern "C" void llvm_fin(u64 opt_level, u64 print_ir_unopt, u64 print_ir, vector<vector<u64> *> *cc_args, vector<u64> *output_file) {
    cg_ret_void();
    if (print_ir_unopt) {
        errs() << "\n=== Unoptimized IR:\n\n";
        module_->print(errs(), nullptr);
    }
    if (verifyModule(*module_, &errs()) == true) {
        if (!print_ir_unopt) {
            errs() << "\n=== Unoptimized IR:\n\n";
            module_->print(errs(), nullptr);
        }
        exit(1);
    }
    legacy::PassManager pm;
    PassManagerBuilder pmb;
    pmb.OptLevel = opt_level;
    if (opt_level >= 2)
        pmb.Inliner = createFunctionInliningPass();
    pmb.populateModulePassManager(pm);
    if (tmpnam(temp_dir_path_buffer) == nullptr) {
        errs() << "Failed to generate temporary filename";
        exit(1);
    }
    string temp_dir_path(temp_dir_path_buffer);
    error_code dir_ec;
    filesystem::create_directory(temp_dir_path, dir_ec);
    if (dir_ec) {
        errs() << "Failed to create directory: " << dir_ec.message() << "\n";
        exit(1);
    }
    string obj_file_path = temp_dir_path + "/obj.o";
    string main_file_path = temp_dir_path + "/main.c";
    error_code file_ec;
    raw_fd_ostream file(obj_file_path, file_ec);
    if (file_ec) {
        errs() << "Failed to open file: " << file_ec.message() << "\n";
        filesystem::remove_all(temp_dir_path);
        exit(1);
    }
    target_machine->addPassesToEmitFile(pm, file, nullptr, CGFT_ObjectFile);
    pm.run(*module_);
    if (print_ir) {
        errs() << "\n=== Optimized IR:\n\n";
        module_->print(errs(), nullptr);
    }
    FILE *main_file = fopen(main_file_path.c_str(), "w");
    if (main_file == nullptr) {
        perror("Failed to open file");
        filesystem::remove_all(temp_dir_path);
        exit(1);
    }
    if (fwrite(program_main_c, 1, program_main_c_len, main_file) != program_main_c_len) {
        fprintf(stderr, "Failed to write to file\n");
        filesystem::remove_all(temp_dir_path);
        exit(1);
    }
    if (fclose(main_file) != 0) {
        perror("Failed to close file");
        filesystem::remove_all(temp_dir_path);
        exit(1);
    }
    string command = "clang";
    command.push_back(' ');
    for (char c : main_file_path)
        command.push_back(c);
    command.push_back(' ');
    for (char c : obj_file_path)
        command.push_back(c);
    command.push_back(' ');
    command.push_back('-');
    command.push_back('o');
    command.push_back(' ');
    for (u64 c : *output_file)
        command.push_back(c);
    for (vector<u64> *cc_arg : *cc_args) {
        command.push_back(' ');
        for (u64 c : *cc_arg)
            command.push_back(c);
    }
    int linker_ret = system(command.c_str());
    filesystem::remove_all(temp_dir_path);
    if (linker_ret != 0)
        exit(1);
}
