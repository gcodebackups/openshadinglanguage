/*
Copyright (c) 2009-2010 Sony Pictures Imageworks Inc., et al.
All Rights Reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.
* Neither the name of Sony Pictures Imageworks nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "llvm_headers.h"
#include "oslexec_pvt.h"
#include "oslops.h"
#include "../liboslcomp/oslcomp_pvt.h"
#include "runtimeoptimize.h"

using namespace OSL;
using namespace OSL::pvt;
using namespace llvm;

#ifdef OSL_NAMESPACE
namespace OSL_NAMESPACE {
#endif

namespace OSL {
namespace pvt {


static ustring op_abs("abs");
static ustring op_add("add");
static ustring op_assign("assign");
static ustring op_ceil("ceil");
static ustring op_color("color");
static ustring op_compref("compref");
static ustring op_cos("cos");
static ustring op_cross("cross");
static ustring op_div("div");
static ustring op_dot("dot");
static ustring op_dowhile("dowhile");
static ustring op_end("end");
static ustring op_eq("eq");
static ustring op_erf("erf");
static ustring op_erfc("erfc");
static ustring op_exp("exp");
static ustring op_exp2("exp2");
static ustring op_expm1("expm1");
static ustring op_fabs("fabs");
static ustring op_for("for");
static ustring op_ge("ge");
static ustring op_gt("gt");
static ustring op_if("if");
static ustring op_le("le");
static ustring op_length("length");
static ustring op_log10("log10");
static ustring op_log2("log2");
static ustring op_logb("logb");
static ustring op_lt("lt");
static ustring op_luminance("luminance");
static ustring op_mod("mod");
static ustring op_mul("mul");
static ustring op_neg("neg");
static ustring op_neq("neq");
static ustring op_nop("nop");
static ustring op_normalize("normalize");
static ustring op_printf("printf");
static ustring op_sin("sin");
static ustring op_sqrt("sqrt");
static ustring op_sub("sub");
static ustring op_vector("vector");
static ustring op_while("while");



/// Macro that defines the arguments to LLVM IR generating routines
///
#define LLVM_IR_GENERATOR_ARGS     RuntimeOptimizer &rop, int opnum

/// Macro that defines the full declaration of a shadeop constant-folder.
/// 
#define DECL_LLVMER(name)  void name (LLVM_IR_GENERATOR_ARGS)

/// Function pointer to an LLVM IR-generating routine
///
typedef void (*OpLLVMer) (LLVM_IR_GENERATOR_ARGS);




const llvm::StructType *
RuntimeOptimizer::getShaderGlobalType ()
{
    std::vector<const Type*> vec3_types(3, Type::getFloatTy(llvm_context()));
    const StructType* vec3_type = StructType::get(llvm_context(), vec3_types);
    const Type* float_type = Type::getFloatTy(llvm_context());
    // NOTE(boulos): Bool is a C++ concept that maps to int in C.
    const Type* bool_type = Type::getInt32Ty(llvm_context());
    const PointerType* void_ptr_type = Type::getInt8PtrTy(llvm_context());

    std::vector<const Type*> sg_types;
    // P, dPdx, dPdy, I, dIdx, dIdy, N, Ng
    for (int i = 0; i < 8; i++) {
        sg_types.push_back(vec3_type);
    }
    // u, v, dudx, dudy, dvdx, dvdy
    for (int i = 0; i < 6; i++) {
        sg_types.push_back(float_type);
    }
    // dPdu, dPdv
    for (int i = 0; i < 2; i++) {
        sg_types.push_back(vec3_type);
    }
    // time, dtime
    for (int i = 0; i < 2; i++) {
        sg_types.push_back(float_type);
    }
    // dPdtime, Ps, dPsdx, dPdsy
    for (int i = 0; i < 4; i++) {
        sg_types.push_back(vec3_type);
    }
    // void* renderstate, object2common, shader2common
    for (int i = 0; i < 3; i++) {
        sg_types.push_back(void_ptr_type);
    }
    // ClosureColor* (treat as void for now?)
    for (int i = 0; i < 1; i++) {
        sg_types.push_back(void_ptr_type);
    }
    // surfacearea
    for (int i = 0; i < 1; i++) {
        sg_types.push_back(float_type);
    }
    // iscameraray, isshadowray, flipHandedness
    for (int i = 0; i < 3; i++) {
        sg_types.push_back(bool_type);
    }

    return StructType::get (llvm_context(), sg_types);
}



static int
ShaderGlobalNameToIndex (ustring name, int deriv)
{
    int sg_index = -1;
    if (name == Strings::P) {
        switch (deriv) {
        case 0: sg_index = 0; break; // P
        case 1: sg_index = 1; break; // dPdx
        case 2: sg_index = 2; break; // dPdy
        default: break;
        }
    } else if (name == Strings::I) {
        switch (deriv) {
        case 0: sg_index = 3; break; // I
        case 1: sg_index = 4; break; // dIdx
        case 2: sg_index = 5; break; // dIdy
        default: break;
        }
    } else if (name == Strings::N) {
        sg_index = 6;
    } else if (name == Strings::Ng) {
        sg_index = 7;
    } else if (name == Strings::u) {
        switch (deriv) {
        case 0: sg_index = 8; break; // u
        case 1: sg_index = 10; break; // dudx
        case 2: sg_index = 11; break; // dudy
        default: break;
        }
    } else if (name == Strings::v) {
        switch (deriv) {
        case 0: sg_index = 9; break; // v
        case 1: sg_index = 12; break; // dvdx
        case 2: sg_index = 13; break; // dvdy
        default: break;
        }
        //extern ustring P, I, N, Ng, dPdu, dPdv, u, v, time, dtime, dPdtime, Ps;
    } else if (name == Strings::dPdu) {
        sg_index = 14;
    } else if (name == Strings::dPdv) {
        sg_index = 15;
    } else if (name == Strings::time) {
        sg_index = 16;
    } else if (name == Strings::dtime) {
        sg_index = 17;
    } else if (name == Strings::dPdtime) {
        sg_index = 18;
    } else if (name == Strings::Ps) {
        switch (deriv) {
        case 0: sg_index = 19; break; // Ps
        case 1: sg_index = 20; break; // dPsdx
        case 2: sg_index = 21; break; // dPsdy
        default: break;
        }
    }
    return sg_index;
}



static bool
SkipSymbol (const Symbol& s)
{
    if (s.symtype() == SymTypeOutputParam)
        return true;

    if (s.typespec().simpletype().basetype == TypeDesc::STRING)
        return true;

    if (s.typespec().is_closure())
        return true;

    if (s.typespec().is_structure())
        return true;

    if (s.symtype() == SymTypeParam) {
        // Skip connections
        if (s.valuesource() == Symbol::ConnectedVal) return true;
        // Skip user-data
        if (!s.lockgeom()) return true;

        // Skip params with init ops
        if (s.initbegin() != s.initend()) return true;
    }

    return false;
}



extern "C" void
llvm_osl_printf (const char* format_str, ...)
{
    // FIXME -- no, no, we need to take a ShadingSys ref and go through
    // the preferred output mechanisms.
    va_list args;
    va_start (args, format_str);
    vprintf (format_str, args);
    va_end (args);
}



llvm::Value *
RuntimeOptimizer::getLLVMSymbolBase (const Symbol &sym)
{
    Symbol* dealiased = sym.dealias();
    std::string mangled_name = dealiased->mangled();
    AllocationMap::iterator map_iter = named_values().find (mangled_name);
    if (map_iter == named_values().end()) {
        shadingsys().error ("Couldn't find symbol '%s' (unmangled = '%s'). Did you forget to allocate it?",
                            mangled_name.c_str(), dealiased->name().c_str());
        return 0;
    }
    return map_iter->second;
}



llvm::Value *
RuntimeOptimizer::getOrAllocateLLVMSymbol (const Symbol& sym,
                                           llvm::Function* f)
{
    Symbol* dealiased = sym.dealias();
    std::string mangled_name = dealiased->mangled();
    AllocationMap::iterator map_iter = named_values().find(mangled_name);

    if (map_iter == named_values().end()) {
        bool has_derivs = sym.has_derivs();
        bool is_float = sym.typespec().is_floatbased();
        int num_components = sym.typespec().simpletype().aggregate;
        int total_size = num_components * (has_derivs ? 3 : 1);
        IRBuilder<> tmp_builder (&f->getEntryBlock(), f->getEntryBlock().begin());
        //shadingsys().info ("Making a type with %d %ss for symbol '%s'\n", total_size, (is_float) ? "float" : "int", mangled_name.c_str());
        llvm::AllocaInst* allocation = 0;
        if (total_size == 1) {
            ConstantInt* type_len = ConstantInt::get(llvm_context(), APInt(32, total_size));
            allocation = tmp_builder.CreateAlloca((is_float) ? Type::getFloatTy(llvm_context()) : Type::getInt32Ty(llvm_context()), type_len, mangled_name.c_str());
        } else {
            std::vector<const Type*> types(total_size, (is_float) ? Type::getFloatTy(llvm_context()) : Type::getInt32Ty(llvm_context()));
            StructType* struct_type = StructType::get(llvm_context(), types);
            ConstantInt* num_structs = ConstantInt::get(llvm_context(), APInt(32, 1));
            allocation = tmp_builder.CreateAlloca(struct_type, num_structs, mangled_name.c_str());
        }

        //outs() << "Allocation = " << *allocation << "\n";
        named_values()[mangled_name] = allocation;
        return allocation;
    }
    return map_iter->second;
}



llvm::Value *
RuntimeOptimizer::LLVMLoadShaderGlobal (const Symbol& sym, int component,
                                        int deriv)
{
    int sg_index = ShaderGlobalNameToIndex (sym.name(), deriv);
    //shadingsys().info ("Global '%s' has sg_index = %d\n", sym.name().c_str(), sg_index);
    if (sg_index == -1) {
        shadingsys().error ("Warning unhandled global '%s'", sym.name().c_str());
        return NULL;
    }

    int num_elements = sym.typespec().simpletype().aggregate;
    int real_component = std::min (num_elements, component);

    llvm::Value* field = builder().CreateConstGEP2_32 (sg_ptr(), 0, sg_index);
    if (num_elements == 1) {
        return builder().CreateLoad (field);
    } else {
        llvm::Value* element = builder().CreateConstGEP2_32(field, 0, real_component);
        return builder().CreateLoad (element);
    }
}



llvm::Value *
RuntimeOptimizer::LLVMStoreShaderGlobal (llvm::Value* val, const Symbol& sym,
                                         int component, int deriv)
{
    shadingsys().error ("WARNING: Store to shaderglobal unsupported!\n");
    return NULL;
}



void
llvm_useparam_op (RuntimeOptimizer &rop, const Symbol& sym, int component, int deriv, 
                  float* fdata, int* idata, ustring* sdata)
{
    // If the param is connected, we need the following sequence:
    // if (!initialized[param]) {
    //   if (!connected_layer[param].already_run()) {
    //     call ConnectedLayer() with sg_ptr;
    //   }
    //   write heap_data[param] into local_params[param];
    // }
}



llvm::Value *
RuntimeOptimizer::LoadParam (const Symbol& sym, int component, int deriv,
                             float* fdata, int* idata, ustring* sdata)
{
    // The local value of the param should have already been filled in
    // by a useparam. So at this point, we just need the following:
    //
    //   return local_params[param];
    //
    return NULL;
}



llvm::Value *
RuntimeOptimizer::loadLLVMValue (const Symbol& sym, int component,
                                 int deriv)
{
    // Regardless of what object this is, if it doesn't have derivs but we're asking for them, return 0
    bool has_derivs = sym.has_derivs();
    if (!has_derivs && deriv != 0) {
        // Asking for the value of our derivative, but we don't have one. Return 0.
        if (sym.typespec().is_floatbased()) {
            return ConstantFP::get (llvm_context(), APFloat(0.f));
        } else {
            return ConstantInt::get (llvm_context(), APInt(32, 0));
        }
    }

    // Handle Globals (and eventually Params) separately since they have
    // aliasing stuff and use a different layout than locals.
    if (sym.symtype() == SymTypeGlobal)
        return LLVMLoadShaderGlobal (sym, component, deriv);

    // Get the pointer of the aggregate (the alloca)
    int num_elements = sym.typespec().simpletype().aggregate;
    int real_component = std::min(num_elements, component);
    int index = real_component + deriv * num_elements;
    //shadingsys().info ("Looking up index %d (comp_%d -> realcomp_%d +  %d * %d) for symbol '%s'\n", index, component, real_component, deriv, num_elements, sym.mangled().c_str());
    llvm::Value* aggregate = getLLVMSymbolBase (sym);
    if (!aggregate) return 0;
    if (num_elements == 1 && !has_derivs) {
        // The thing is just a scalar
        return builder().CreateLoad (aggregate);
    } else {
#if 1
        llvm::Value* ptr = builder().CreateConstGEP2_32 (aggregate, 0, index);
        //outs() << "aggregate = " << *aggregate << " and GEP = " << *ptr << "\n";
        return builder().CreateLoad (ptr);
#else
        return builder().CreateExtractValue (aggregate, index);
#endif
    }
}



void
RuntimeOptimizer::storeLLVMValue (Value* new_val, const Symbol& sym,
                                  int component, int deriv)
{
    bool has_derivs = sym.has_derivs();
    if (!has_derivs && deriv != 0) {
        shadingsys().error ("Tried to store to symbol '%s', component %d, deriv_idx %d but doesn't have derivatives\n", sym.name().c_str(), component, deriv);
        return;
    }

    llvm::Value* aggregate = getLLVMSymbolBase (sym);
    if (!aggregate)
        return;

    int num_elements = sym.typespec().simpletype().aggregate;
    if (num_elements == 1 && !has_derivs) {
        builder().CreateStore (new_val, aggregate);
    } else {
        int real_component = std::min(num_elements, component);
        int index = real_component + deriv * num_elements;
#if 1
        //shadingsys().info ("Storing value into index %d (comp_%d -> realcomp_%d +  %d * %d) for symbol '%s'\n", index, component, real_component, deriv, num_elements, sym.mangled().c_str());
        llvm::Value* ptr = builder().CreateConstGEP2_32 (aggregate, 0, index);
        builder().CreateStore (new_val, ptr);
#else
        builder().CreateInsertValue (aggregate, new_val, index);
#endif
    }
}



void
llvm_gen_printf (RuntimeOptimizer &rop, int opnum)
{
    Opcode &op (rop.inst()->ops()[opnum]);
    llvm::Function *llvm_printf_func = rop.llvm_module()->getFunction("llvm_osl_printf");

    // Prepare the args for the call
    SmallVector<Value*, 16> call_args;
    Symbol& format_sym = *rop.opargsym (op, 0);
    if (!format_sym.is_constant()) {
        rop.shadingsys().warning ("printf must currently have constant format\n");
        return;
    }

    // We're going to need to adjust the format string as we go, but I'd
    // like to reserve a spot for the char*.
    call_args.push_back(NULL);

    ustring format_ustring = *((ustring*)format_sym.data());
    const char* format = format_ustring.c_str();
    std::string s;
    int arg = 0;
    while (*format != '\0') {
        if (*format == '%') {
            if (format[1] == '%') {
                // '%%' is a literal '%'
                s += "%%";
                format += 2;  // skip both percentages
                continue;
            }
            const char *oldfmt = format;  // mark beginning of format
            while (*format &&
                   *format != 'c' && *format != 'd' && *format != 'e' &&
                   *format != 'f' && *format != 'g' && *format != 'i' &&
                   *format != 'm' && *format != 'n' && *format != 'o' &&
                   *format != 'p' && *format != 's' && *format != 'u' &&
                   *format != 'v' && *format != 'x' && *format != 'X')
                ++format;
            ++format; // Also eat the format char
            if (arg >= op.nargs()) {
                rop.shadingsys().error ("Mismatch between format string and arguments");
                return;
            }

            std::string ourformat (oldfmt, format);  // straddle the format
            // Doctor it to fix mismatches between format and data
            Symbol& sym (*rop.opargsym (op, 1 + arg));
            if (SkipSymbol(sym)) {
                rop.shadingsys().warning ("symbol type for '%s' unsupported for printf\n", sym.mangled().c_str());
                return;
            }
            TypeDesc simpletype (sym.typespec().simpletype());
            int num_components = simpletype.numelements() * simpletype.aggregate;
            // NOTE(boulos): Only for debug mode do the derivatives get printed...
            for (int i = 0; i < num_components; i++) {
                if (i != 0) s += " ";
                s += ourformat;

                llvm::Value* loaded = rop.loadLLVMValue (sym, i, 0);
                // NOTE(boulos): Annoyingly varargs makes it so that things need
                // to be upconverted from float to double (who knew?)
                if (sym.typespec().is_floatbased()) {
                    call_args.push_back (rop.builder().CreateFPExt(loaded, Type::getDoubleTy(rop.llvm_context())));
                } else {
                    call_args.push_back (loaded);
                }
            }
            ++arg;
        } else if (*format == '\\') {
            // Escape sequence
            ++format;  // skip the backslash
            switch (*format) {
            case 'n' : s += '\n';     break;
            case 'r' : s += '\r';     break;
            case 't' : s += '\t';     break;
            default:   s += *format;  break;  // Catches '\\' also!
            }
            ++format;
        } else {
            // Everything else -- just copy the character and advance
            s += *format++;
        }
    }


    //shadingsys().info ("llvm printf. Original string = '%s', new string = '%s'",
    //     format_ustring.c_str(), s.c_str());

    llvm::Value* llvm_str = rop.builder().CreateGlobalString(s.c_str());
    llvm::Value* llvm_ptr = rop.builder().CreateConstGEP2_32(llvm_str, 0, 0);
    //outs() << "Type of format string (CreateGlobalString) = " << *llvm_str << "\n";
    //outs() << "llvm_ptr after GEP = " << *llvm_ptr << "\n";
    call_args[0] = llvm_ptr;

    // Get llvm_osl_printf from the module
    rop.builder().CreateCall(llvm_printf_func, call_args.begin(), call_args.end());
    //outs() << "printf_call = " << *printf_call << "\n";
}



llvm::Value *
RuntimeOptimizer::llvm_float_to_int (llvm::Value* fval)
{
    return builder().CreateFPToSI(fval, Type::getInt32Ty(llvm_context()));
}



llvm::Value *
RuntimeOptimizer::llvm_int_to_float (llvm::Value* ival)
{
    return builder().CreateSIToFP(ival, Type::getFloatTy(llvm_context()));
}



// Simple (pointwise) binary ops (+-*/)
void
llvm_gen_binary_op (RuntimeOptimizer &rop, int opnum)
{
    Opcode &op (rop.inst()->ops()[opnum]);

    Symbol& dst  = *rop.opargsym (op, 0);
    Symbol& src1 = *rop.opargsym (op, 1);
    Symbol& src2 = *rop.opargsym (op, 2);
    if (SkipSymbol(dst) ||
        SkipSymbol(src1) ||
        SkipSymbol(src2))
        return;

    bool dst_derivs = dst.has_derivs();
    int num_components = dst.typespec().simpletype().aggregate;
    bool is_float = dst.typespec().is_floatbased();

    bool src1_float = src1.typespec().is_floatbased();
    bool src2_float = src2.typespec().is_floatbased();

    for (int i = 0; i < num_components; i++) {
        // Get src1/2 component i
        llvm::Value* src1_load = rop.loadLLVMValue (src1, i, 0);
        llvm::Value* src2_load = rop.loadLLVMValue (src2, i, 0);

        if (!src1_load) return;
        if (!src2_load) return;

        llvm::Value* src1_val = src1_load;
        llvm::Value* src2_val = src2_load;

        bool need_float_op = src1_float || src2_float;
        if (need_float_op) {
            // upconvert int -> float for the op if necessary
            if (src1_float && !src2_float) {
                src2_val = rop.llvm_int_to_float (src2_load);
            } else if (!src1_float && src2_float) {
                src1_val = rop.llvm_int_to_float (src1_load);
            } else {
                // both floats, do nothing
            }
        }

        // Perform the op
        llvm::Value* result = 0;
        ustring opname = op.opname();

        // Upconvert the value if necessary fr

        if (opname == op_add) {
            result = (need_float_op) ? rop.builder().CreateFAdd(src1_val, src2_val) : rop.builder().CreateAdd(src1_val, src2_val);
        } else if (opname == op_sub) {
            result = (need_float_op) ? rop.builder().CreateFSub(src1_val, src2_val) : rop.builder().CreateSub(src1_val, src2_val);
        } else if (opname == op_mul) {
            result = (need_float_op) ? rop.builder().CreateFMul(src1_val, src2_val) : rop.builder().CreateMul(src1_val, src2_val);
        } else if (opname == op_div) {
            result = (need_float_op) ? rop.builder().CreateFDiv(src1_val, src2_val) : rop.builder().CreateSDiv(src1_val, src2_val);
        } else if (opname == op_mod) {
            result = (need_float_op) ? rop.builder().CreateFRem(src1_val, src2_val) : rop.builder().CreateSRem(src1_val, src2_val);
        } else {
            // Don't know how to handle this.
            rop.shadingsys().error ("Don't know how to handle op '%s', eliding the store\n", opname.c_str());
        }

        // Store the result
        if (result) {
            // if our op type doesn't match result, convert
            if (is_float && !need_float_op) {
                // Op was int, but we need to store float
                result = rop.llvm_int_to_float (result);
            } else if (!is_float && need_float_op) {
                // Op was float, but we need to store int
                result = rop.llvm_float_to_int (result);
            } // otherwise just fine
            rop.storeLLVMValue (result, dst, i, 0);
        }

        if (dst_derivs) {
            // mul results in <a * b, a * b_dx + b * a_dx, a * b_dy + b * a_dy>
            rop.shadingsys().info ("punting on derivatives for now\n");
        }
    }
}



// Simple (pointwise) unary ops (Neg, Abs, Sqrt, Ceil, Floor, ..., Log2,
// Log10, Erf, Erfc, IsNan/IsInf/IsFinite)
void
llvm_gen_unary_op (RuntimeOptimizer &rop, int opnum)
{
    Opcode &op (rop.inst()->ops()[opnum]);
    Symbol& dst  = *rop.opargsym (op, 0);
    Symbol& src = *rop.opargsym (op, 1);
    if (SkipSymbol(dst) ||
        SkipSymbol(src))
        return;

    bool dst_derivs = dst.has_derivs();
    int num_components = dst.typespec().simpletype().aggregate;

    bool dst_float = dst.typespec().is_floatbased();
    bool src_float = src.typespec().is_floatbased();

    for (int i = 0; i < num_components; i++) {
        // Get src1/2 component i
        llvm::Value* src_load = rop.loadLLVMValue (src, i, 0);
        if (!src_load) return;

        llvm::Value* src_val = src_load;

        // Perform the op
        llvm::Value* result = 0;
        ustring opname = op.opname();

        if (opname == op_neg) {
            result = (src_float) ? rop.builder().CreateFNeg(src_val) : rop.builder().CreateNeg(src_val);
        } else if (opname == op_abs ||
                   opname == op_fabs) {
            if (src_float) {
                // Call fabsf
                const Type* float_ty = Type::getFloatTy(rop.llvm_context());
                llvm::Value* fabsf_func = rop.llvm_module()->getOrInsertFunction("fabsf", float_ty, float_ty, NULL);
                result = rop.builder().CreateCall(fabsf_func, src_val);
            } else {
                // neg_version = -x
                // result = (x < 0) ? neg_version : x
                llvm::Value* negated = rop.builder().CreateNeg(src_val);
                llvm::Value* cond = rop.builder().CreateICmpSLT(src_val, ConstantInt::get(Type::getInt32Ty(rop.llvm_context()), 0));
                result = rop.builder().CreateSelect(cond, negated, src_val);
            }
        } else if (opname == op_sqrt && src_float) {
            const Type* float_ty = Type::getFloatTy(rop.llvm_context());
            result = rop.builder().CreateCall(Intrinsic::getDeclaration(rop.llvm_module(), Intrinsic::sqrt, &float_ty, 1), src_val);
        } else if (opname == op_sin && src_float) {
            const Type* float_ty = Type::getFloatTy(rop.llvm_context());
            result = rop.builder().CreateCall(Intrinsic::getDeclaration(rop.llvm_module(), Intrinsic::sin, &float_ty, 1), src_val);
        } else if (opname == op_cos && src_float) {
            const Type* float_ty = Type::getFloatTy(rop.llvm_context());
            result = rop.builder().CreateCall(Intrinsic::getDeclaration(rop.llvm_module(), Intrinsic::cos, &float_ty, 1), src_val);
        } else {
            // Don't know how to handle this.
            rop.shadingsys().error ("Don't know how to handle op '%s', eliding the store\n", opname.c_str());
        }

        // Store the result
        if (result) {
            // if our op type doesn't match result, convert
            if (dst_float && !src_float) {
                // Op was int, but we need to store float
                result = rop.llvm_int_to_float (result);
            } else if (!dst_float && src_float) {
                // Op was float, but we need to store int
                result = rop.llvm_float_to_int (result);
            } // otherwise just fine
            rop.storeLLVMValue (result, dst, i, 0);
        }

        if (dst_derivs) {
            // mul results in <a * b, a * b_dx + b * a_dx, a * b_dy + b * a_dy>
            rop.shadingsys().info ("punting on derivatives for now\n");
            // FIXME!!
        }
    }
}



// Simple assignment
void
llvm_gen_assign (RuntimeOptimizer &rop, int opnum)
{
    Opcode &op (rop.inst()->ops()[opnum]);
    Symbol& dst  = *rop.opargsym (op, 0);
    Symbol& src = *rop.opargsym (op, 1);
    if (SkipSymbol(dst) ||
        SkipSymbol(src))
        return;

    bool dst_derivs = dst.has_derivs();
    int num_components = dst.typespec().simpletype().aggregate;

    bool dst_float = dst.typespec().is_floatbased();
    bool src_float = src.typespec().is_floatbased();

    //rop.shadingsys().info ("assigning '%s' (mangled = '%s') to '%s' (mangled = '%s')\n", src.name().c_str(), src.mangled().c_str(), dst.name().c_str(), dst.mangled().c_str());

    for (int i = 0; i < num_components; i++) {
        // Get src component i
        llvm::Value* src_val = rop.loadLLVMValue (src, i, 0);
        if (!src_val) return;

        // Perform the assignment
        if (dst_float && !src_float) {
            // need int -> float
            src_val = rop.builder().CreateSIToFP(src_val, Type::getFloatTy(rop.llvm_context()));
        } else if (!dst_float && src_float) {
            // float -> int
            src_val = rop.builder().CreateFPToSI(src_val, Type::getInt32Ty(rop.llvm_context()));
        }
        rop.storeLLVMValue (src_val, dst, i, 0);

        if (dst_derivs) {
            // mul results in <a * b, a * b_dx + b * a_dx, a * b_dy + b * a_dy>
            rop.shadingsys().info ("punting on derivatives for now\n");
            // FIXME!!!
        }
    }
}



// Component reference
void
llvm_gen_compref (RuntimeOptimizer &rop, int opnum)
{
    Opcode &op (rop.inst()->ops()[opnum]);
    Symbol& dst = *rop.opargsym (op, 0);
    Symbol& src = *rop.opargsym (op, 1);
    Symbol& index = *rop.opargsym (op, 2);
    if (SkipSymbol(dst) ||
        SkipSymbol(src) ||
        SkipSymbol(index))
        return;

    bool dst_derivs = dst.has_derivs();
    int num_components = src.typespec().simpletype().aggregate;

    bool dst_float = dst.typespec().is_floatbased();
    bool src_float = src.typespec().is_floatbased();

    // Get src component index
    if (!index.is_constant()) {
        rop.shadingsys().info ("punting on non-constant index for now. annoying\n");
        // FIXME
        return;
    }
    int const_index = *((int*)index.data());
    if (const_index < 0 || const_index >= num_components) {
        rop.shadingsys().warning ("index out of range for object (idx = %d, num_comp = %d)\n", const_index, num_components);
        return;
    }

    llvm::Value* src_val = rop.loadLLVMValue (src, const_index, 0);
    if (!src_val) return;

    // Perform the assignment
    if (dst_float && !src_float) {
        // need int -> float
        src_val = rop.builder().CreateSIToFP(src_val, Type::getFloatTy(rop.llvm_context()));
    } else if (!dst_float && src_float) {
        // float -> int
        src_val = rop.builder().CreateFPToSI(src_val, Type::getInt32Ty(rop.llvm_context()));
    }

    // compref is: scalar = vector[int]
    rop.storeLLVMValue (src_val, dst, 0, 0);

    if (dst_derivs) {
        // mul results in <a * b, a * b_dx + b * a_dx, a * b_dy + b * a_dy>
        rop.shadingsys().info ("punting on derivatives for now\n");
        // FIXME
    }
}



// Simple aggregate constructor (no conversion)
void
llvm_gen_construct_aggregate (RuntimeOptimizer &rop, int opnum)
{
    Opcode &op (rop.inst()->ops()[opnum]);
    Symbol& dst = *rop.opargsym (op, 0);
    Symbol& arg1 = *rop.opargsym (op, 1);
    if (arg1.typespec().is_string()) {
        // Using a string to say what space we want, punt for now.
        return;  // FIXME
    }
    // Otherwise, the args are just data.
    const Symbol* src_syms[16];
    for (int i = 1; i < op.nargs(); i++)
        src_syms[i-1] = rop.opargsym (op, i);

    bool dst_derivs = dst.has_derivs();
    int num_components = dst.typespec().simpletype().aggregate;

    bool dst_float = dst.typespec().is_floatbased();

    for (int i = 0; i < num_components; i++) {
        const Symbol& src = *src_syms[i];
        bool src_float = src.typespec().is_floatbased();
        // Get src component 0 (it should be a scalar)
        llvm::Value* src_val = rop.loadLLVMValue (src, 0, 0);
        if (!src_val) return;

        // Perform the assignment
        if (dst_float && !src_float) {
            // need int -> float
            src_val = rop.builder().CreateSIToFP(src_val, Type::getFloatTy(rop.llvm_context()));
        } else if (!dst_float && src_float) {
            // float -> int
            src_val = rop.builder().CreateFPToSI(src_val, Type::getInt32Ty(rop.llvm_context()));
        }
        rop.storeLLVMValue (src_val, dst, i, 0);

        if (dst_derivs) {
            // mul results in <a * b, a * b_dx + b * a_dx, a * b_dy + b * a_dy>
            rop.shadingsys().info ("punting on derivatives for now\n");
            // FIXME
        }
    }
}



// Comparison ops (though other binary -> scalar ops like dot might end
// up being similar)
void
llvm_gen_compare_op (RuntimeOptimizer &rop, int opnum)
{
    Opcode &op (rop.inst()->ops()[opnum]);
    Symbol& dst  = *rop.opargsym (op, 0);
    Symbol& src1 = *rop.opargsym (op, 1);
    Symbol& src2 = *rop.opargsym (op, 2);
    if (SkipSymbol(dst) ||
        SkipSymbol(src1) ||
        SkipSymbol(src2))
        return;

    bool dst_derivs = dst.has_derivs();
    int num_components = dst.typespec().simpletype().aggregate;

    bool src1_float = src1.typespec().is_floatbased();
    bool src2_float = src2.typespec().is_floatbased();

    llvm::Value* final_result = 0;

    for (int i = 0; i < num_components; i++) {
        // Get src1/2 component i
        llvm::Value* src1_load = rop.loadLLVMValue (src1, i, 0);
        llvm::Value* src2_load = rop.loadLLVMValue (src2, i, 0);

        if (!src1_load) return;
        if (!src2_load) return;

        llvm::Value* src1_val = src1_load;
        llvm::Value* src2_val = src2_load;

        bool need_float_op = src1_float || src2_float;
        if (need_float_op) {
            // upconvert int -> float for the op if necessary
            if (src1_float && !src2_float) {
                src2_val = rop.llvm_int_to_float (src2_load);
            } else if (!src1_float && src2_float) {
                src1_val = rop.llvm_int_to_float (src1_load);
            } else {
                // both floats, do nothing
            }
        }

        // Perform the op
        llvm::Value* result = 0;

        ustring opname = op.opname();

        // Upconvert the value if necessary fr

        if (opname == op_lt) {
            result = (need_float_op) ? rop.builder().CreateFCmpULT(src1_val, src2_val) : rop.builder().CreateICmpSLT(src1_val, src2_val);
        } else if (opname == op_le) {
            result = (need_float_op) ? rop.builder().CreateFCmpULE(src1_val, src2_val) : rop.builder().CreateICmpSLE(src1_val, src2_val);
        } else if (opname == op_eq) {
            result = (need_float_op) ? rop.builder().CreateFCmpUEQ(src1_val, src2_val) : rop.builder().CreateICmpEQ(src1_val, src2_val);
        } else if (opname == op_ge) {
            result = (need_float_op) ? rop.builder().CreateFCmpUGE(src1_val, src2_val) : rop.builder().CreateICmpSGE(src1_val, src2_val);
        } else if (opname == op_gt) {
            result = (need_float_op) ? rop.builder().CreateFCmpUGT(src1_val, src2_val) : rop.builder().CreateICmpSGT(src1_val, src2_val);
        } else if (opname == op_neq) {
            result = (need_float_op) ? rop.builder().CreateFCmpUNE(src1_val, src2_val) : rop.builder().CreateICmpNE(src1_val, src2_val);
        } else {
            // Don't know how to handle this.
            rop.shadingsys().error ("Don't know how to handle op '%s', eliding the store\n", opname.c_str());
        }

        if (result) {
            if (final_result) {
                // Combine the component bool based on the op
                if (opname != op_neq) {
                    // final_result &= result
                    final_result = rop.builder().CreateAnd(final_result, result);
                } else {
                    // final_result |= result
                    final_result = rop.builder().CreateOr(final_result, result);
                }
            } else {
                final_result = result;
            }
        }
    }

    if (final_result) {
        // Convert the single bit bool into an int for now.
        final_result = rop.builder().CreateZExt(final_result, Type::getInt32Ty(rop.llvm_context()));

        bool is_float = dst.typespec().is_floatbased();
        if (is_float) {
            final_result = rop.llvm_int_to_float (final_result);
        }

        rop.storeLLVMValue (final_result, dst, 0, 0);
        if (dst_derivs) {
            // deriv of conditional!?
            rop.shadingsys().info ("punting on derivatives for now\n");
            // FIXME
        }
    }
}



// unary reduction ops (length, luminance, determinant (much more complicated...))
void
llvm_gen_unary_reduction (RuntimeOptimizer &rop, int opnum)
{
    Opcode &op (rop.inst()->ops()[opnum]);
    Symbol& dst  = *rop.opargsym (op, 0);
    Symbol& src = *rop.opargsym (op, 1);
    if (SkipSymbol(dst) ||
        SkipSymbol(src))
        return;

    bool dst_derivs = dst.has_derivs();
    // Loop over the source
    int num_components = src.typespec().simpletype().aggregate;

    llvm::Value* final_result = 0;
    ustring opname = op.opname();

    for (int i = 0; i < num_components; i++) {
        // Get src1/2 component i
        llvm::Value* src_load = rop.loadLLVMValue (src, i, 0);

        if (!src_load) return;

        llvm::Value* src_val = src_load;

        // Perform the op
        llvm::Value* result = 0;

        if (opname == op_length) {
            result = rop.builder().CreateFMul(src_val, src_val);
        } else if (opname == op_luminance) {
            float coeff = 0.f;
            switch (i) {
            case 0: coeff = .2126f; break;
            case 1: coeff = .7152f; break;
            default: coeff = .0722f; break;
            }
            result = rop.builder().CreateFMul(src_val, ConstantFP::get(rop.llvm_context(), APFloat(coeff)));
        } else {
            // Don't know how to handle this.
            rop.shadingsys().error ("Don't know how to handle op '%s', eliding the store\n", opname.c_str());
        }

        if (result) {
            if (final_result) {
                final_result = rop.builder().CreateFAdd(final_result, result);
            } else {
                final_result = result;
            }
        }
    }

    if (final_result) {
        // Compute sqrtf(result) if it's length instead of luminance
        if (opname == op_length) {
            // Take sqrt
            const Type* float_ty = Type::getFloatTy(rop.llvm_context());
            final_result = rop.builder().CreateCall(Intrinsic::getDeclaration(rop.llvm_module(), Intrinsic::sqrt, &float_ty, 1), final_result);
        }

        rop.storeLLVMValue (final_result, dst, 0, 0);
        if (dst_derivs) {
            rop.shadingsys().info ("punting on derivatives for now\n");
            // FIXME
        }
    }
}



// dot. This is could easily be a more general f(Agg, Agg) -> Scalar,
// but we don't seem to have any others.
void
llvm_gen_dot (RuntimeOptimizer &rop, int opnum)
{
    Opcode &op (rop.inst()->ops()[opnum]);
    Symbol& dst  = *rop.opargsym (op, 0);
    Symbol& src1 = *rop.opargsym (op, 1);
    Symbol& src2 = *rop.opargsym (op, 2);
    if (SkipSymbol(dst) ||
        SkipSymbol(src1) ||
        SkipSymbol(src2))
        return;

    bool dst_derivs = dst.has_derivs();
    // Loop over the sources
    int num_components = src1.typespec().simpletype().aggregate;

    llvm::Value* final_result = 0;

    for (int i = 0; i < num_components; i++) {
        // Get src1/src2 component i
        llvm::Value* src1_load = rop.loadLLVMValue (src1, i, 0);
        llvm::Value* src2_load = rop.loadLLVMValue (src2, i, 0);

        if (!src1_load || !src2_load) return;

        llvm::Value* result = rop.builder().CreateFMul(src1_load, src2_load);

        if (final_result) {
            final_result = rop.builder().CreateFAdd(final_result, result);
        } else {
            final_result = result;
        }
    }

    rop.storeLLVMValue (final_result, dst, 0, 0);
    if (dst_derivs) {
        rop.shadingsys().info ("punting on derivatives for now\n");
        // FIXME
    }
}



// cross.
void
llvm_gen_cross (RuntimeOptimizer &rop, int opnum)
{
    Opcode &op (rop.inst()->ops()[opnum]);
    Symbol& dst  = *rop.opargsym (op, 0);
    Symbol& src1 = *rop.opargsym (op, 1);
    Symbol& src2 = *rop.opargsym (op, 2);
    if (SkipSymbol(dst) ||
        SkipSymbol(src1) ||
        SkipSymbol(src2))
        return;

    bool dst_derivs = dst.has_derivs();
    int num_components = dst.typespec().simpletype().aggregate;

    for (int i = 0; i < num_components; i++) {
        // Get src1/src2 component for output i
        int src1_idx0[3] = { 1, 2, 0 };
        int src1_idx1[3] = { 2, 0, 1 };

        int src2_idx0[3] = { 2, 0, 1 };
        int src2_idx1[3] = { 1, 2, 0 };

        llvm::Value* src1_load0 = rop.loadLLVMValue (src1, src1_idx0[i], 0);
        llvm::Value* src1_load1 = rop.loadLLVMValue (src1, src1_idx1[i], 0);

        llvm::Value* src2_load0 = rop.loadLLVMValue (src2, src2_idx0[i], 0);
        llvm::Value* src2_load1 = rop.loadLLVMValue (src2, src2_idx1[i], 0);

        if (!src1_load0 || !src1_load1 || !src2_load0 || !src2_load1) return;

        llvm::Value* prod0 = rop.builder().CreateFMul(src1_load0, src2_load0);
        llvm::Value* prod1 = rop.builder().CreateFMul(src1_load1, src2_load1);
        llvm::Value* result = rop.builder().CreateFSub(prod0, prod1);

        rop.storeLLVMValue (result, dst, i, 0);
        if (dst_derivs) {
            rop.shadingsys().info ("punting on derivatives for now\n");
            // FIXME
        }
    }
}



// normalize. This is sort of like unary with a side product (length)
// that we need to then apply to the whole vector. TODO(boulos): Try
// to reuse the length code maybe?
void
llvm_gen_normalize (RuntimeOptimizer &rop, int opnum)
{
    Opcode &op (rop.inst()->ops()[opnum]);
    Symbol& dst  = *rop.opargsym (op, 0);
    Symbol& src = *rop.opargsym (op, 1);
    if (SkipSymbol(dst) ||
        SkipSymbol(src))
        return;

    bool dst_derivs = dst.has_derivs();
    int num_components = dst.typespec().simpletype().aggregate;

    llvm::Value* length_squared = 0;

    for (int i = 0; i < num_components; i++) {
        // Get src component i
        llvm::Value* src_load = rop.loadLLVMValue (src, i, 0);

        if (!src_load) return;

        llvm::Value* src_val = src_load;

        // Perform the op
        llvm::Value* result = rop.builder().CreateFMul(src_val, src_val);

        if (length_squared) {
            length_squared = rop.builder().CreateFAdd(length_squared, result);
        } else {
            length_squared = result;
        }
    }

    // Take sqrt
    const Type* float_ty = Type::getFloatTy(rop.llvm_context());
    llvm::Value* length = rop.builder().CreateCall(Intrinsic::getDeclaration(rop.llvm_module(), Intrinsic::sqrt, &float_ty, 1), length_squared);
    // Compute 1/length
    llvm::Value* inv_length = rop.builder().CreateFDiv(ConstantFP::get(rop.llvm_context(), APFloat(1.f)), length);

    for (int i = 0; i < num_components; i++) {
        // Get src component i
        llvm::Value* src_load = rop.loadLLVMValue (src, i, 0);

        if (!src_load) return;

        llvm::Value* src_val = src_load;

        // Perform the op (src_val * inv_length is the order in opvector.cpp)
        llvm::Value* result = rop.builder().CreateFMul(src_val, inv_length);

        rop.storeLLVMValue (result, dst, i, 0);
        if (dst_derivs) {
            rop.shadingsys().info ("punting on derivatives for now\n");
            // FIXME
        }
    }
}



void
llvm_gen_if (RuntimeOptimizer &rop, int opnum)
{
    Opcode &op (rop.inst()->ops()[opnum]);
    Symbol& cond = *rop.opargsym (op, 0);
    if (SkipSymbol(cond))
        return;
    RuntimeOptimizer::BasicBlockMap& bb_map (rop.bb_map());

    // Load the condition variable
    llvm::Value* cond_load = rop.loadLLVMValue (cond, 0, 0);
    // Convert the int to a bool via truncation
    llvm::Value* cond_bool = rop.builder().CreateTrunc (cond_load, Type::getInt1Ty(rop.llvm_context()));
    // Branch on the condition, to our blocks
    BasicBlock* then_block = bb_map[opnum+1];
    BasicBlock* else_block = bb_map[op.jump(0)];
    BasicBlock* after_block = bb_map[op.jump(1)];
    rop.builder().CreateCondBr (cond_bool, then_block, else_block);
    // Put an unconditional branch at the end of the Then and Else blocks
    if (then_block != after_block) {
        rop.builder().SetInsertPoint (then_block);
        rop.builder().CreateBr (after_block);
    }
    if (else_block != after_block) {
        rop.builder().SetInsertPoint (else_block);
        rop.builder().CreateBr (after_block);
    }
}



void
llvm_gen_loop_op (RuntimeOptimizer &rop, int opnum)
{
    Opcode &op (rop.inst()->ops()[opnum]);
    Symbol& cond = *rop.opargsym (op, 0);
    if (SkipSymbol(cond))
        return;
    RuntimeOptimizer::BasicBlockMap& bb_map (rop.bb_map());

    // Branch on the condition, to our blocks
    BasicBlock* init_block = bb_map[opnum+1];
    BasicBlock* cond_block = bb_map[op.jump(0)];
    BasicBlock* body_block = bb_map[op.jump(1)];
    BasicBlock* step_block = bb_map[op.jump(2)];
    BasicBlock* after_block = bb_map[op.jump(3)];

    // Insert the unconditional jump to the LoopCond
    if (init_block != cond_block) {
        // There are some init ops, insert branch afterwards (but first jump to InitBlock)
        rop.builder().CreateBr(init_block);
        rop.builder().SetInsertPoint(init_block);
    }
    // Either we have init ops (and we'll jump to LoopCond afterwards)
    // or we don't and we need a terminator in the current block. If
    // we're a dowhile loop, we jump to the body block after init
    // instead of cond.
    if (op.opname() == op_dowhile) {
        rop.builder().CreateBr(body_block);
    } else {
        rop.builder().CreateBr(cond_block);
    }

    rop.builder().SetInsertPoint(cond_block);
    // Load the condition variable (it will have been computed by now)
    llvm::Value* cond_load = rop.loadLLVMValue (cond, 0, 0);
    // Convert the int to a bool via truncation
    llvm::Value* cond_bool = rop.builder().CreateTrunc(cond_load, Type::getInt1Ty(rop.llvm_context()));
    // Jump to either LoopBody or AfterLoop
    rop.builder().CreateCondBr(cond_bool, body_block, after_block);

    // Put an unconditional jump from Body into Step
    if (step_block != after_block) {
        rop.builder().SetInsertPoint(body_block);
        rop.builder().CreateBr(step_block);

        // Put an unconditional jump from Step to Cond
        rop.builder().SetInsertPoint(step_block);
        rop.builder().CreateBr(cond_block);
    } else {
        // Step is empty, probably a do_while or while loop. Jump from Body to Cond
        rop.builder().SetInsertPoint(body_block);
        rop.builder().CreateBr(cond_block);
    }
}



void
RuntimeOptimizer::llvm_assign_initial_constant (const Symbol& sym)
{
    ASSERT (sym.is_constant() && ! sym.has_derivs());
    int num_components = sym.typespec().simpletype().aggregate;
    bool is_float = sym.typespec().is_floatbased();
    for (int i = 0; i < num_components; ++i) {
        // Fill in the constant val
        // Setup initial store
        llvm::Value* init_val = 0;
        // shadingsys.info ("Assigning initial value for symbol '%s' = ", sym.mangled().c_str());
        if (is_float) {
            float fval = ((float*)sym.data())[i];
            init_val = ConstantFP::get(llvm_context(), APFloat(fval));
            // shadingsys.info ("%f\n", fval);
        } else {
            int ival = ((int*)sym.data())[i];
            init_val = ConstantInt::get(llvm_context(), APInt(32, ival));
            // shadingsys.info ("%d\n", ival);
        }
        storeLLVMValue (init_val, sym, i, 0);
    }
}




static std::map<ustring,OpLLVMer> llvm_generator_table;


static void
initialize_llvm_generator_table ()
{
    static spin_mutex table_mutex;
    static bool table_initialized = false;
    spin_lock lock (table_mutex);
    if (table_initialized)
        return;   // already initialized
#define INIT2(name,func) llvm_generator_table[ustring(#name)] = func
#define INIT(name) llvm_generator_table[ustring(#name)] = llvm_gen_##name;

    INIT (assign);
    INIT2 (add, llvm_gen_binary_op);
    INIT2 (sub, llvm_gen_binary_op);
    INIT2 (mul, llvm_gen_binary_op);
    INIT2 (div, llvm_gen_binary_op);
    INIT2 (mod, llvm_gen_binary_op);
    INIT (dot);
    INIT (cross);
    INIT (normalize);
    INIT (compref);
    INIT2 (eq, llvm_gen_compare_op);
    INIT2 (neq, llvm_gen_compare_op);
    INIT2 (lt, llvm_gen_compare_op);
    INIT2 (le, llvm_gen_compare_op);
    INIT2 (gt, llvm_gen_compare_op);
    INIT2 (ge, llvm_gen_compare_op);
    INIT2 (neg, llvm_gen_unary_op);
    INIT2 (abs, llvm_gen_unary_op);
    INIT2 (fabs, llvm_gen_unary_op);
    INIT2 (sqrt, llvm_gen_unary_op);
    INIT2 (sin, llvm_gen_unary_op);
    INIT2 (cos, llvm_gen_unary_op);
    INIT2 (vector, llvm_gen_construct_aggregate);
    INIT2 (color, llvm_gen_construct_aggregate);
    INIT2 (length, llvm_gen_unary_reduction);
    INIT2 (luminance, llvm_gen_unary_reduction);
    INIT (if);
    INIT2 (for, llvm_gen_loop_op);
    INIT2 (while, llvm_gen_loop_op);
    INIT2 (dowhile, llvm_gen_loop_op);
    INIT (printf);

#undef INIT
#undef INIT2

    table_initialized = true;
}



llvm::Function*
RuntimeOptimizer::build_llvm_version ()
{
    llvm::Module *all_ops (m_llvm_module);
    m_named_values.clear ();

    // I'd like our new function to take just a ShaderGlobals...
    char unique_layer_name[1024];
    sprintf (unique_layer_name, "%s_%d", inst()->layername().c_str(), inst()->id());
    const llvm::StructType* sg_type = getShaderGlobalType ();
    llvm::PointerType* sg_ptr_type = PointerType::get(sg_type, 0 /* Address space */);
    // Make a layer function: void layer_func(ShaderGlobal*)
    llvm::Function* layer_func = cast<Function>(all_ops->getOrInsertFunction(unique_layer_name, Type::getVoidTy(*m_llvm_context), sg_ptr_type, NULL));
    const OpcodeVec& instance_ops (inst()->ops());
    Function::arg_iterator arg_it = layer_func->arg_begin();
    // Get shader globals pointer
    m_llvm_shaderglobals_ptr = arg_it++;

    BasicBlock* entry_bb = BasicBlock::Create(*m_llvm_context, "EntryBlock", layer_func);

    delete m_builder;
    m_builder = new IRBuilder<> (entry_bb);

    // Setup the symbols
    BOOST_FOREACH (Symbol &s, inst()->symbols()) {
        if (SkipSymbol(s))
            continue;
        // Don't allocate globals
        if (s.symtype() == SymTypeGlobal)
            continue;
        // Make space
        getOrAllocateLLVMSymbol (s, layer_func);
        if (s.is_constant())
            llvm_assign_initial_constant (s);
    }

    // All the symbols are stack allocated now.

    // Go learn about the BasicBlock's we'll need to make. NOTE(boulos):
    // The definition of BasicBlock here follows the LLVM version which
    // differs from that in runtimeoptimize.cpp. In particular, the
    // instructions in a Then block are part of a new
    // BasicBlock. QUESTION(boulos): This actually should be true for
    // runtimeoptimize as well. If you happened to define a variable in
    // a condition (which is bad mojo, but legal) the aliasing stuff is
    // probably wrong...
    std::vector<bool> bb_start (instance_ops.size(), false);

    for (size_t opnum = 0; opnum < instance_ops.size(); ++opnum) {
        const Opcode& op = instance_ops[opnum];
        if (op.opname() == op_if) {
            // For a true BasicBlock, since we are going to conditionally
            // jump into the ThenBlock, we need to label the next
            // instruction as starting ThenBlock.
            bb_start[opnum+1] = true;
            // The ElseBlock also can be jumped to
            bb_start[op.jump(0)] = true;
            // And ExitBlock
            bb_start[op.jump(1)] = true;
        } else if (op.opname() == op_for ||
                   op.opname() == op_while ||
                   op.opname() == op_dowhile) {
            bb_start[opnum+1] = true; // LoopInit
            bb_start[op.jump(0)] = true; // LoopCond
            bb_start[op.jump(1)] = true; // LoopBody
            bb_start[op.jump(2)] = true; // LoopStep
            bb_start[op.jump(3)] = true; // AfterLoop
        }
    }

    // Create a map from ops with bb_start=true to their BasicBlock*
    m_bb_map.clear ();
    for (size_t opnum = 0; opnum < instance_ops.size(); ++opnum) {
        if (bb_start[opnum])
            m_bb_map[opnum] = BasicBlock::Create (*m_llvm_context, "", layer_func);
    }

    for (size_t opnum = 0; opnum < instance_ops.size(); ++opnum) {
        const Opcode& op = instance_ops[opnum];
        if (bb_start[opnum]) {
            // If we start a new BasicBlock, point the builder there.
            BasicBlock* next_bb = m_bb_map[opnum];
            if (next_bb != entry_bb) {
                // If we're not the entry block (which is where all the
                // AllocaInstructions go), then start insertion at the
                // beginning of the block. This way we can insert instructions
                // before the possible jmp inserted at the end by an upstream
                // conditional (e.g. if/for/while/do)
                builder().SetInsertPoint(next_bb, next_bb->begin());
            } else {
                // Otherwise, use the end (IRBuilder default)
                builder().SetInsertPoint(next_bb);
            }
        }

        //rop.shadingsys().info ("op%03zu: %s\n", i, op.opname().c_str());

        std::map<ustring,OpLLVMer>::const_iterator found;
        found = llvm_generator_table.find (op.opname());
        if (found != llvm_generator_table.end()) {
            (*found->second) (*this, opnum);
        } else if (op.opname() == op_nop ||
                   op.opname() == op_end) {
            // Skip this op, it does nothing...
        } else {
            m_shadingsys.error ("LLVMOSL: Unsupported op %s\n", op.opname().c_str());
            return NULL;
        }
    }

    builder().CreateRetVoid();

    outs() << "layer_func (" << unique_layer_name << ") after llvm  = " << *layer_func << "\n";

    // Now optimize the result
    shadingsys().FunctionOptimizer()->run(*layer_func);

    outs() << "layer_func (" << unique_layer_name << ") after opt  = " << *layer_func << "\n";

    inst()->llvm_version = layer_func;

    delete m_builder;
    m_builder = NULL;

    return layer_func;
}



void
ShadingSystemImpl::SetupLLVM ()
{
    // Setup already
    if (m_llvm_exec != NULL)
        return;
    info ("Setting up LLVM");
    m_llvm_context = new llvm::LLVMContext();
    info ("Initializing Native Target");
    llvm::InitializeNativeTarget();

    //printf("Loading LLVM Bitcode\n");
    m_llvm_module = new llvm::Module ("oslmodule", *llvm_context());

    info ("Building an Execution Engine");
    std::string error_msg;

    //m_llvm_exec = llvm::EngineBuilder(m_llvm_module).setErrorStr(&error_msg).create();
    m_llvm_exec = llvm::ExecutionEngine::create(m_llvm_module,
                                                false,
                                                &error_msg,
                                                llvm::CodeGenOpt::Default,
                                                false);
    if (! m_llvm_exec) {
        error ("Failed to create engine: %s\n", error_msg.c_str());
        return;
    }

    //shadingsys().info ("Disabling lazy JIT\n");
    //m_llvm_exec->DisableLazyCompilation();
    info ("Setting up pass managers");
    SetupLLVMOptimizer();
    //IPOOptimizer()->run(*all_ops);
    //shadingsys().info ("LLVM ready!\n");

    info ("Adding in extern functions");
    std::vector<const llvm::Type*> printf_params;
    printf_params.push_back (llvm::Type::getInt8PtrTy(*llvm_context()));
    llvm::FunctionType* printf_type = llvm::FunctionType::get (llvm::Type::getVoidTy(*llvm_context()), printf_params, true /* varargs */);
    m_llvm_module->getOrInsertFunction ("llvm_osl_printf", printf_type);

    initialize_llvm_generator_table ();
}



void
ShadingSystemImpl::SetupLLVMOptimizer ()
{
    //m_opt_ipo = new PassManager();
    //m_opt_ipo->add(llvm::createFunctionInliningPass(2000));

    info ("Making FunctionPassManager");
    m_llvm_opt_function = new llvm::FunctionPassManager(m_llvm_module);
    info ("Adding TargetInfo");
    m_llvm_opt_function->add (new llvm::TargetData(*(m_llvm_exec->getTargetData())));
    // Now change things to registers
    info ("Adding mem2reg");
    m_llvm_opt_function->add (llvm::createPromoteMemoryToRegisterPass());
    // Combine instructions where possible
    info ("Adding instcomb");
    m_llvm_opt_function->add (llvm::createInstructionCombiningPass());
    // resassociate exprssions (a = x + (3 + y) -> a = x + y + 3)
    info ("Adding reassoc");
    m_llvm_opt_function->add (llvm::createReassociatePass());
    // eliminate common sub-expressions
    info ("Adding gvn");
    m_llvm_opt_function->add (llvm::createGVNPass());
    // Simplify the call graph if possible
    info ("Adding simpcfg");
    m_llvm_opt_function->add (llvm::createCFGSimplificationPass());

    info ("Adding DCE");
    m_llvm_opt_function->add (llvm::createAggressiveDCEPass());
    // Try to make stuff into registers one last time.
    info ("Adding mem2reg (again)");
    m_llvm_opt_function->add (llvm::createPromoteMemoryToRegisterPass());

    // Always add verifier?
    info ("Adding verifier");
    m_llvm_opt_function->add (llvm::createVerifierPass());

    info ("Performing init");
    m_llvm_opt_function->doInitialization();
}



}; // namespace pvt
}; // namespace osl

#ifdef OSL_NAMESPACE
}; // end namespace OSL_NAMESPACE
#endif