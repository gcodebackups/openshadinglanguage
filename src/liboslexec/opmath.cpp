/*
Copyright (c) 2009 Sony Pictures Imageworks, et al.
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

/////////////////////////////////////////////////////////////////////////
/// \file
///
/// Shader interpreter implementation of basic math operators 
/// such as +, -, *, /, %.
///
/////////////////////////////////////////////////////////////////////////


#include <iostream>

#include "oslexec_pvt.h"
#include "oslops.h"
#include "oslclosure.h"

#include "OpenImageIO/varyingref.h"


#ifdef OSL_NAMESPACE
namespace OSL_NAMESPACE {
#endif
namespace OSL {
namespace pvt {


namespace {

// Make a templated functor that encapsulates addition.
template<class R, class A, class B>
class Add {
public:
    Add (ShadingExecution *) { }
    inline R operator() (const A &a, const B &b) { return R (a + b); }
};


// Make a templated functor that encapsulates subtraction.
template<class R, class A, class B>
class Sub {
public:
    Sub (ShadingExecution *) { }
    inline R operator() (const A &a, const B &b) { return R (a - b); }
};


// Make a templated functor that encapsulates multiplication.
template<class R, class A, class B>
class Mul {
public:
    Mul (ShadingExecution *) { }
    inline R operator() (const A &a, const B &b) { return R (a * b); }
};

template<>
class Mul<Matrix44,Matrix44,int> {
public:
    Mul (ShadingExecution *) { }
    inline Matrix44 operator() (const Matrix44 &a, int b) {
        return Matrix44 (a * (float)b);
    }
};

template<>
class Mul<Matrix44,int,Matrix44> {
public:
    Mul (ShadingExecution *) { }
    inline Matrix44 operator() (int a, const Matrix44 &b) {
        return Matrix44 ((float)a * b);
    }
};

// Specialized version for matrix = scalar * scalar
class ScalarMatrixMul {
public:
    ScalarMatrixMul (ShadingExecution *) { }
    inline Matrix44 operator() (float a, float b) {
        float f = a * b;
        return Matrix44 (f,0,0,0, 0,f,0,0, 0,0,f,0, 0,0,0,f);
    }
};

// Make a templated functor that encapsulates division.
template<class R, class A, class B>
class Div {
public:
    Div (ShadingExecution *) { }
    inline R operator() (const A &a, const B &b) {
        return (b == (Float)0.0) ? R (0.0) : R (a / b);
    }
};

// Specialized version for matrix = matrix / matrix
template<>
class Div<Matrix44,Matrix44,Matrix44>
{
public:
    Div (ShadingExecution *) { }
    inline Matrix44 operator() (const Matrix44 &a, const Matrix44 &b) {
        return a * b.inverse();
    }
};

// Specialized version for matrix = float / matrix
template<>
class Div<Matrix44,float,Matrix44>
{
public:
    Div (ShadingExecution *) { }
    inline Matrix44 operator() (float a, const Matrix44 &b) {
        return a * b.inverse();
    }
};

// Specialized version for matrix = int / matrix
template<>
class Div<Matrix44,int,Matrix44>
{
public:
    Div (ShadingExecution *) { }
    inline Matrix44 operator() (int a, const Matrix44 &b) {
        return (float)a * b.inverse();
    }
};

// Specialized version for matrix = matrix / int
template<>
class Div<Matrix44,Matrix44,int>
{
public:
    Div (ShadingExecution *) { }
    inline Matrix44 operator() (const Matrix44 &a, int b) {
        return a / (float)b;
    }
};

// Specialized version for matrix = scalar / scalar
class ScalarMatrixDiv {
public:
    ScalarMatrixDiv (ShadingExecution *) { }
    inline Matrix44 operator() (float a, float b) {
        float f = (b == 0) ? 0.0 : (a / b);
        return Matrix44 (f,0,0,0, 0,f,0,0, 0,0,f,0, 0,0,0,f);
    }
};

// Make a functor that encapsulates modulus
class Mod {
public:
    Mod (ShadingExecution *exec) : m_exec(exec) { }
    inline int operator() (int a, int b) { return safe_mod(a, b); }
    inline float operator() (float x, float y) { return safe_fmod(x, y); }
    inline Vec3 operator() (const Vec3 &x, float y) { return safe_fmod(x, y); }
    inline Vec3 operator() (const Vec3 &x, const Vec3 &y) { return safe_fmod(x, y); }
private:
    inline int safe_mod(int a, int b) {
        if (b == 0) {
             m_exec->error ("attempted to compute mod(%d, %d)", a, b);
            return 0;
        }
        else {
            return (a % b);
        }
    }
    inline float safe_fmod (float x, float y) { 
        if (y == 0.0f) {
             m_exec->error ("attempted to compute mod(%g, %g)", x, y);
            return 0.0f;
        }
        else {
            return fmodf (x,y); 
        }
    }
    inline Vec3 safe_fmod (const Vec3 &x, float y) { 
        if (y == 0.0f) {
             m_exec->error ("attempted to compute mod(%g %g %g, %g)",
                            x[0], x[1], x[2], y);
            return Vec3 (0.0f, 0.0f, 0.0f);
        }
        else {
            return Vec3 (fmodf( x[0],y), fmodf(x[1],y), fmodf (x[2],y));
        }
    }
    inline Vec3 safe_fmod (const Vec3 &x, const Vec3 &y) { 
        if (y[0] == 0.0f || y[1] == 0.0f || y[2] == 0.0f) {
             m_exec->error ("attempted to compute mod(%g %g %g, %g %g %g)",
                            x[0], x[1], x[2], y[0], y[1], y[2]);
            float x0 = (y[0] == 0.0f) ? 0.0f : fmodf (x[0], y[0]);
            float x1 = (y[1] == 0.0f) ? 0.0f : fmodf (x[1], y[1]);
            float x2 = (y[2] == 0.0f) ? 0.0f : fmodf (x[2], y[2]);
            return Vec3 (x0, x1, x2);
        }
        else {
            return Vec3 (fmodf( x[0],y[0]), fmodf(x[1],y[1]), fmodf (x[2],y[2]));
        }
    }
    ShadingExecution *m_exec;
};

// Make a templated functor that encapsulates negation.
template<class R, class A>
class Neg {
public:
    Neg (ShadingExecution *) { }
    inline R operator() (const A &a) { return R (-a); }
};



// Specialized binary operation driver for closures.  We actually store
// the pointers to the closures, so r = op(a,b) won't work properly.
// What we really want is *r = op(*a,*b), but we don't want the copy
// either.  So we use a functor that takes arguments (result, a, b) and
// do not return a value.
template <class ATYPE, class BTYPE, class FUNCTION>
DECLOP (closure_binary_op)
{
    // Get references to the symbols this op accesses
    Symbol &Result (exec->sym (args[0]));
    Symbol &A (exec->sym (args[1]));
    Symbol &B (exec->sym (args[2]));

    // Adjust the result's uniform/varying status
    exec->adjust_varying (Result, true /* closures always vary */);

    // Loop over points, do the operation
    VaryingRef<ClosureColor *> result ((ClosureColor **)Result.data(), Result.step());
    VaryingRef<ATYPE> a ((ATYPE *)A.data(), A.step());
    VaryingRef<BTYPE> b ((BTYPE *)B.data(), B.step());
    FUNCTION function (exec);
    for (int i = beginpoint;  i < endpoint;  ++i)
        if (runflags[i])
            function (result[i], a[i], b[i]);
}



// Specialized unary operation driver for closures.  We actually store
// the pointers to the closures, so r = op(a) won't work properly.
// What we really want is *r = op(*a), but we don't want the copy
// either.  So we use a functor that takes arguments (result, a) and
// do not return a value.
template <class ATYPE, class FUNCTION>
DECLOP (closure_unary_op)
{
    // Get references to the symbols this op accesses
    Symbol &Result (exec->sym (args[0]));
    Symbol &A (exec->sym (args[1]));

    // Adjust the result's uniform/varying status
    exec->adjust_varying (Result, true /* closures always vary */);

    // Loop over points, do the operation
    VaryingRef<ClosureColor *> result ((ClosureColor **)Result.data(), Result.step());
    VaryingRef<ATYPE> a ((ATYPE *)A.data(), A.step());
    FUNCTION function (exec);
    for (int i = beginpoint;  i < endpoint;  ++i)
        if (runflags[i])
            function (result[i], a[i]);
}



class AddClosure {
public:
    AddClosure (ShadingExecution *) { }
    inline void operator() (ClosureColor *result, 
                            const ClosureColor *A, const ClosureColor *B) {
        result->add (*A, *B);
    }
};


class SubClosure {
public:
    SubClosure (ShadingExecution *) { }
    inline void operator() (ClosureColor *result, 
                            const ClosureColor *A, const ClosureColor *B) {
        result->sub (*A, *B);
    }
};


class MulClosure {
public:
    MulClosure (ShadingExecution *) { }
    inline void operator() (ClosureColor *result, 
                            const ClosureColor *A, const Color3 &B) {
        *result = *A;
        *result *= B;
    }
    inline void operator() (ClosureColor *result, 
                            const Color3 &A, const ClosureColor *B) {
        *result = *B;
        *result *= A;
    }
    inline void operator() (ClosureColor *result, 
                            const ClosureColor *A, float B) {
        *result = *A;
        *result *= B;
    }
    inline void operator() (ClosureColor *result, 
                            float A, const ClosureColor *B) {
        *result = *B;
        *result *= A;
    }
};


class DivClosure {
public:
    DivClosure (ShadingExecution *) { }
    inline void operator() (ClosureColor *result, 
                            const ClosureColor *A, const Color3 &B) {
        *result = *A;
        *result *= Color3 (1.0/B[0], 1.0/B[1], 1.0/B[2]);
    }
    inline void operator() (ClosureColor *result, 
                            const ClosureColor *A, float B) {
        *result = *A;
        *result *= ((Float)1.0) / B;
    }
};


class NegClosure {
public:
    NegClosure (ShadingExecution *) { }
    inline void operator() (ClosureColor *result, const ClosureColor *A) {
        *result = *A;
        *result *= -1.0;
    }
};


};  // End anonymous namespace




DECLOP (OP_add)
{
    ASSERT (nargs == 3);
    Symbol &Result (exec->sym (args[0]));
    Symbol &A (exec->sym (args[1]));
    Symbol &B (exec->sym (args[2]));
    ASSERT (! Result.typespec().is_structure() &&
            ! Result.typespec().is_array());   // Not yet
    ASSERT (! A.typespec().is_structure() &&
            ! A.typespec().is_array());   // Not yet
    ASSERT (! B.typespec().is_structure() &&
            ! B.typespec().is_array());   // Not yet
    OpImpl impl = NULL;

    if (Result.typespec().is_closure()) {
        if (A.typespec().is_closure() && B.typespec().is_closure())
            impl = closure_binary_op<ClosureColor *, ClosureColor *, AddClosure>;
    }

    else if (Result.typespec().is_triple()) {
        if (A.typespec().is_triple()) {
            if (B.typespec().is_triple())
                impl = binary_op<Vec3,Vec3,Vec3, Add<Vec3,Vec3,Vec3> >;
            else if (B.typespec().is_float())
                impl = binary_op<VecProxy,VecProxy,float,
                                 Add<VecProxy,VecProxy,float> >;
            else if (B.typespec().is_int())
                impl = binary_op<VecProxy,VecProxy,int,
                                 Add<VecProxy,VecProxy,int> >;
        } else if (A.typespec().is_float()) {
            if (B.typespec().is_triple())
                impl = binary_op<VecProxy,float,VecProxy,
                                 Add<VecProxy,float,VecProxy> >;
        } if (A.typespec().is_int()) {
            if (B.typespec().is_triple())
                impl = binary_op<VecProxy,int,VecProxy,
                                 Add<VecProxy,int,VecProxy> >;
        }
    } 

    else if (Result.typespec().is_float()) {
        if (A.typespec().is_float() && B.typespec().is_float())
            impl = binary_op<float,float,float, Add<float,float,float> >;
        else if (A.typespec().is_float() && B.typespec().is_int())
            impl = binary_op<float,float,int, Add<float,float,int> >;
        else if (A.typespec().is_int() && B.typespec().is_float())
            impl = binary_op<float,int,float, Add<float,int,float> >;
    }

    else if (Result.typespec().is_int()) {
        if (A.typespec().is_int() && B.typespec().is_int())
            impl = binary_op<int,int,int, Add<int,int,int> >;
    }

    if (impl) {
        impl (exec, nargs, args, runflags, beginpoint, endpoint);
        // Use the specialized one for next time!  Never have to check the
        // types or do the other sanity checks again.
        // FIXME -- is this thread-safe?
        exec->op().implementation (impl);
    } else {
        std::cerr << "Don't know how to add " << Result.typespec().string()
                  << " = " << A.typespec().string() 
                  << " + " << B.typespec().string() << "\n";
        ASSERT (0 && "Addition types can't be handled");
    }
}



DECLOP (OP_sub)
{
    ASSERT (nargs == 3);
    Symbol &Result (exec->sym (args[0]));
    Symbol &A (exec->sym (args[1]));
    Symbol &B (exec->sym (args[2]));
    ASSERT (! Result.typespec().is_structure() &&
            ! Result.typespec().is_array());   // Not yet
    ASSERT (! A.typespec().is_structure() &&
            ! A.typespec().is_array());   // Not yet
    ASSERT (! B.typespec().is_structure() &&
            ! B.typespec().is_array());   // Not yet
    OpImpl impl = NULL;

    if (Result.typespec().is_closure()) {
        if (A.typespec().is_closure() && B.typespec().is_closure())
            impl = closure_binary_op<ClosureColor *, ClosureColor *, SubClosure>;
    }

    else if (Result.typespec().is_triple()) {
        if (A.typespec().is_triple()) {
            if (B.typespec().is_triple())
                impl = binary_op<Vec3,Vec3,Vec3, Sub<Vec3,Vec3,Vec3> >;
            else if (B.typespec().is_float())
                impl = binary_op<VecProxy,VecProxy,float,
                                 Sub<VecProxy,VecProxy,float> >;
            else if (B.typespec().is_int())
                impl = binary_op<VecProxy,VecProxy,int,
                                 Sub<VecProxy,VecProxy,int> >;
        } else if (A.typespec().is_float()) {
            if (B.typespec().is_triple())
                impl = binary_op<VecProxy,float,VecProxy,
                                 Sub<VecProxy,float,VecProxy> >;
        } if (A.typespec().is_int()) {
            if (B.typespec().is_triple())
                impl = binary_op<VecProxy,int,VecProxy,
                                 Sub<VecProxy,int,VecProxy> >;
        }
    } 

    else if (Result.typespec().is_float()) {
        if (A.typespec().is_float() && B.typespec().is_float())
            impl = binary_op<float,float,float, Sub<float,float,float> >;
        else if (A.typespec().is_float() && B.typespec().is_int())
            impl = binary_op<float,float,int, Sub<float,float,int> >;
        else if (A.typespec().is_int() && B.typespec().is_float())
            impl = binary_op<float,int,float, Sub<float,int,float> >;
    }

    else if (Result.typespec().is_int()) {
        if (A.typespec().is_int() && B.typespec().is_int())
            impl = binary_op<int,int,int, Sub<int,int,int> >;
    }

    if (impl) {
        impl (exec, nargs, args, runflags, beginpoint, endpoint);
        // Use the specialized one for next time!  Never have to check the
        // types or do the other sanity checks again.
        // FIXME -- is this thread-safe?
        exec->op().implementation (impl);
    } else {
        std::cerr << "Don't know how to sub " << Result.typespec().string()
                  << " = " << A.typespec().string() 
                  << " + " << B.typespec().string() << "\n";
        ASSERT (0 && "Subtraction types can't be handled");
    }
}



DECLOP (OP_mul)
{
    ASSERT (nargs == 3);
    Symbol &Result (exec->sym (args[0]));
    Symbol &A (exec->sym (args[1]));
    Symbol &B (exec->sym (args[2]));
    ASSERT (! Result.typespec().is_structure() &&
            ! Result.typespec().is_array());   // Not yet
    ASSERT (! A.typespec().is_structure() &&
            ! A.typespec().is_array());   // Not yet
    ASSERT (! B.typespec().is_structure() &&
            ! B.typespec().is_array());   // Not yet
    OpImpl impl = NULL;

    if (Result.typespec().is_closure()) {
        ASSERT (A.typespec().is_closure() || B.typespec().is_closure());
        if (A.typespec().is_closure() && B.typespec().is_triple())
            impl = closure_binary_op<ClosureColor *, Color3, MulClosure>;
        else if (A.typespec().is_closure() && B.typespec().is_float())
            impl = closure_binary_op<ClosureColor *, float, MulClosure>;
        else if (A.typespec().is_triple() && B.typespec().is_closure())
            impl = closure_binary_op<Color3, ClosureColor *, MulClosure>;
        else if (A.typespec().is_float() && B.typespec().is_closure())
            impl = closure_binary_op<float, ClosureColor *, MulClosure>;
    }

    else if (Result.typespec().is_triple()) {
        if (A.typespec().is_triple()) {
            if (B.typespec().is_triple())
                impl = binary_op<Vec3,Vec3,Vec3, Mul<Vec3,Vec3,Vec3> >;
            else if (B.typespec().is_float())
                impl = binary_op<VecProxy,VecProxy,float,
                                 Mul<VecProxy,VecProxy,float> >;
            else if (B.typespec().is_int())
                impl = binary_op<VecProxy,VecProxy,int,
                                 Mul<VecProxy,VecProxy,int> >;
        } else if (A.typespec().is_float()) {
            if (B.typespec().is_triple())
                impl = binary_op<VecProxy,float,VecProxy,
                                 Mul<VecProxy,float,VecProxy> >;
        } if (A.typespec().is_int()) {
            if (B.typespec().is_triple())
                impl = binary_op<VecProxy,int,VecProxy,
                                 Mul<VecProxy,int,VecProxy> >;
        }
    } 

    else if (Result.typespec().is_float()) {
        if (A.typespec().is_float() && B.typespec().is_float())
            impl = binary_op<float,float,float, Mul<float,float,float> >;
        else if (A.typespec().is_float() && B.typespec().is_int())
            impl = binary_op<float,float,int, Mul<float,float,int> >;
        else if (A.typespec().is_int() && B.typespec().is_float())
            impl = binary_op<float,int,float, Mul<float,int,float> >;
    }

    else if (Result.typespec().is_int()) {
        if (A.typespec().is_int() && B.typespec().is_int())
            impl = binary_op<int,int,int, Mul<int,int,int> >;
    }

    else if (Result.typespec().is_matrix()) {
        if (A.typespec().is_float()) {
            if (B.typespec().is_float())
                impl = binary_op<Matrix44,float,float, ScalarMatrixMul>;
            else if (B.typespec().is_int())
                impl = binary_op<Matrix44,float,int, ScalarMatrixMul>;
            else if (B.typespec().is_matrix())
                impl = binary_op<Matrix44,float,Matrix44, Mul<Matrix44,float,Matrix44> >;
        } if (A.typespec().is_int()) {
            if (B.typespec().is_float())
                impl = binary_op<Matrix44,int,float, ScalarMatrixMul>;
            else if (B.typespec().is_int())
                impl = binary_op<Matrix44,int,int, ScalarMatrixMul>;
            else if (B.typespec().is_matrix())
                impl = binary_op<Matrix44,int,Matrix44, Mul<Matrix44,int,Matrix44> >;
        } if (A.typespec().is_matrix()) {
            if (B.typespec().is_float())
                impl = binary_op<Matrix44,Matrix44,float, Mul<Matrix44,Matrix44,float> >;
            else if (B.typespec().is_int())
                impl = binary_op<Matrix44,Matrix44,int, Mul<Matrix44,Matrix44,int> >;
            else if (B.typespec().is_matrix())
                impl = binary_op<Matrix44,Matrix44,Matrix44, Mul<Matrix44,Matrix44,Matrix44> >;
        }
    }

    if (impl) {
        impl (exec, nargs, args, runflags, beginpoint, endpoint);
        // Use the specialized one for next time!  Never have to check the
        // types or do the other sanity checks again.
        // FIXME -- is this thread-safe?
        exec->op().implementation (impl);
    } else {
        std::cerr << "Don't know how to mul " << Result.typespec().string()
                  << " = " << A.typespec().string() 
                  << " + " << B.typespec().string() << "\n";
        ASSERT (0 && "Multiplication types can't be handled");
    }
}



DECLOP (OP_div)
{
    // FIXME -- maybe we can speed up div for the case where A is varying
    // and B is uniform, by taking 1/b and mutiplying.

    ASSERT (nargs == 3);
    Symbol &Result (exec->sym (args[0]));
    Symbol &A (exec->sym (args[1]));
    Symbol &B (exec->sym (args[2]));
    ASSERT (! Result.typespec().is_closure() &&
            ! Result.typespec().is_structure() &&
            ! Result.typespec().is_array());   // Not yet
    ASSERT (! A.typespec().is_closure() &&
            ! A.typespec().is_structure() &&
            ! A.typespec().is_array());   // Not yet
    ASSERT (! B.typespec().is_closure() &&
            ! B.typespec().is_structure() &&
            ! B.typespec().is_array());   // Not yet
    OpImpl impl = NULL;

    if (Result.typespec().is_closure()) {
        // FIXME -- not handled yet
    }

    else if (Result.typespec().is_triple()) {
        if (A.typespec().is_triple()) {
            if (B.typespec().is_triple())
                impl = binary_op<VecProxy,VecProxy,VecProxy,
                                 Div<VecProxy,VecProxy,VecProxy> >;
            else if (B.typespec().is_float())
                impl = binary_op<VecProxy,VecProxy,float,
                                 Div<VecProxy,VecProxy,float> >;
            else if (B.typespec().is_int())
                impl = binary_op<VecProxy,VecProxy,int,
                                 Div<VecProxy,VecProxy,int> >;
        } else if (A.typespec().is_float()) {
            if (B.typespec().is_triple())
                impl = binary_op<VecProxy,float,VecProxy,
                                 Div<VecProxy,float,VecProxy> >;
        } if (A.typespec().is_int()) {
            if (B.typespec().is_triple())
                impl = binary_op<VecProxy,int,VecProxy,
                                 Div<VecProxy,int,VecProxy> >;
        }
    } 

    else if (Result.typespec().is_float()) {
        if (A.typespec().is_float() && B.typespec().is_float())
            impl = binary_op<float,float,float, Div<float,float,float> >;
        else if (A.typespec().is_float() && B.typespec().is_int())
            impl = binary_op<float,float,int, Div<float,float,int> >;
        else if (A.typespec().is_int() && B.typespec().is_float())
            impl = binary_op<float,int,float, Div<float,int,float> >;
    }

    else if (Result.typespec().is_int()) {
        if (A.typespec().is_int() && B.typespec().is_int())
            impl = binary_op<int,int,int, Div<int,int,int> >;
    }

    else if (Result.typespec().is_matrix()) {
        if (A.typespec().is_float()) {
            if (B.typespec().is_float())
                impl = binary_op<Matrix44,float,float, ScalarMatrixDiv>;
            else if (B.typespec().is_int())
                impl = binary_op<Matrix44,float,int, ScalarMatrixDiv>;
            else if (B.typespec().is_matrix())
                impl = binary_op<Matrix44,float,Matrix44, Div<Matrix44,float,Matrix44> >;
        } if (A.typespec().is_int()) {
            if (B.typespec().is_float())
                impl = binary_op<Matrix44,int,float, ScalarMatrixDiv>;
            else if (B.typespec().is_int())
                impl = binary_op<Matrix44,int,int, ScalarMatrixDiv>;
            else if (B.typespec().is_matrix())
                impl = binary_op<Matrix44,int,Matrix44, Div<Matrix44,int,Matrix44> >;
        } if (A.typespec().is_matrix()) {
            if (B.typespec().is_float())
                impl = binary_op<Matrix44,Matrix44,float, Div<Matrix44,Matrix44,float> >;
            else if (B.typespec().is_int())
                impl = binary_op<Matrix44,Matrix44,int, Div<Matrix44,Matrix44,int> >;
            else if (B.typespec().is_matrix())
                impl = binary_op<Matrix44,Matrix44,Matrix44, Div<Matrix44,Matrix44,Matrix44> >;
        }
    }

    if (impl) {
        impl (exec, nargs, args, runflags, beginpoint, endpoint);
        // Use the specialized one for next time!  Never have to check the
        // types or do the other sanity checks again.
        // FIXME -- is this thread-safe?
        exec->op().implementation (impl);
    } else {
        std::cerr << "Don't know how to div " << Result.typespec().string()
                  << " = " << A.typespec().string() 
                  << " + " << B.typespec().string() << "\n";
        ASSERT (0 && "Division types can't be handled");
    }
}



DECLOP (OP_mod)
{
    ASSERT (nargs == 3);
    Symbol &Result (exec->sym (args[0]));
    Symbol &A (exec->sym (args[1]));
    Symbol &B (exec->sym (args[2]));
    ASSERT (! Result.typespec().is_closure() &&
            ! Result.typespec().is_structure() &&
            ! Result.typespec().is_array());   // Not yet
    ASSERT (! A.typespec().is_closure() &&
            ! A.typespec().is_structure() &&
            ! A.typespec().is_array());   // Not yet
    ASSERT (! B.typespec().is_closure() &&
            ! B.typespec().is_structure() &&
            ! B.typespec().is_array());   // Not yet
    OpImpl impl = NULL;

    if (Result.typespec().is_int() && A.typespec().is_int() &&
            B.typespec().is_int()) {
        impl = binary_op<int,int,int, Mod >;
    }
    else if (Result.typespec().is_float() && A.typespec().is_float() &&
            B.typespec().is_float()) {
        impl = binary_op<float,float,float, Mod >;
    }
    else if (Result.typespec().is_triple() && A.typespec().is_triple() &&
            B.typespec().is_float()) {
        impl = binary_op<Vec3,Vec3,float, Mod >;
    }
    else if (Result.typespec().is_triple() && A.typespec().is_triple() &&
            B.typespec().is_triple()) {
        impl = binary_op<Vec3,Vec3,Vec3, Mod >;
    }

    if (impl) {
        impl (exec, nargs, args, runflags, beginpoint, endpoint);
        // Use the specialized one for next time!  Never have to check the
        // types or do the other sanity checks again.
        // FIXME -- is this thread-safe?
        exec->op().implementation (impl);
    } else {
        std::cerr << "Don't know how to mod " << Result.typespec().string()
                  << " = " << A.typespec().string() 
                  << " + " << B.typespec().string() << "\n";
        ASSERT (0 && "Division types can't be handled");
    }
}



DECLOP (OP_neg)
{
    ASSERT (nargs == 2);
    Symbol &Result (exec->sym (args[0]));
    Symbol &A (exec->sym (args[1]));
    ASSERT (! Result.typespec().is_structure() &&
            ! Result.typespec().is_array());   // Not yet
    ASSERT (! A.typespec().is_structure() &&
            ! A.typespec().is_array());   // Not yet
    OpImpl impl = NULL;

    if (Result.typespec().is_closure()) {
        if (A.typespec().is_closure())
            impl = closure_unary_op<ClosureColor *, NegClosure>;
    }

    else if (Result.typespec().is_triple()) {
        if (A.typespec().is_triple())
            impl = unary_op<Vec3,Vec3, Neg<Vec3,Vec3> >;
        else if (A.typespec().is_float())
            impl = unary_op<VecProxy,float, Neg<VecProxy,float> >;
        else if (A.typespec().is_int())
            impl = unary_op<VecProxy,int, Neg<VecProxy,int> >;
    } 

    else if (Result.typespec().is_float()) {
        if (A.typespec().is_float())
            impl = unary_op<float,float, Neg<float,float> >;
        else if (A.typespec().is_int())
            impl = unary_op<float,int, Neg<float,int> >;
    }

    else if (Result.typespec().is_int() && A.typespec().is_int()) {
        impl = unary_op<int,int, Neg<int,int> >;
    }

    else if (Result.typespec().is_matrix()) {
        if (A.typespec().is_matrix())
            impl = unary_op<Matrix44,Matrix44, Neg<Matrix44,Matrix44> >;
    }

    if (impl) {
        impl (exec, nargs, args, runflags, beginpoint, endpoint);
        // Use the specialized one for next time!  Never have to check the
        // types or do the other sanity checks again.
        // FIXME -- is this thread-safe?
        exec->op().implementation (impl);
    } else {
        std::cerr << "Don't know how to neg " << Result.typespec().string()
                  << " = -" << A.typespec().string() << "\n";
        ASSERT (0 && "Negation types can't be handled");
    }
}


}; // namespace pvt
}; // namespace OSL
#ifdef OSL_NAMESPACE
}; // end namespace OSL_NAMESPACE
#endif