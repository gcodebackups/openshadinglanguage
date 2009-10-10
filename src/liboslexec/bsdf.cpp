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

#include <cmath>

#include "oslops.h"
#include "oslexec_pvt.h"


#ifdef OSL_NAMESPACE
namespace OSL_NAMESPACE {
#endif
namespace OSL {



/// Given values x and y on [0,1], convert them in place to values on
/// [-1,1] uniformly distributed over a unit sphere.  This code is
/// derived from Peter Shirley, "Realistic Ray Tracing", p. 103.
static void
to_unit_disk (float &x, float &y)
{
    float r, phi;
    float a = 2.0f * x - 1.0f;
    float b = 2.0f * y - 1.0f;
    if (a > -b) {
        if (a > b) {
            r = a;
	         phi = M_PI_4 * (b/a);
	     } else {
	         r = b;
	         phi = M_PI_4 * (2.0f - a/b);
	     }
    } else {
        if (a < b) {
            r = -a;
            phi = M_PI_4 * (4.0f + b/a);
        } else {
            r = -b;
            if (b != 0.0f)
                phi = M_PI_4 * (6.0f - a/b);
            else
                phi = 0.0f;
        }
    }
    x = r * cosf (phi);
    y = r * sinf (phi);
}



/// Make two unit vectors that are orthogonal to N and each other.  This
/// assumes that N is already normalized.  We get the first orthonormal
/// by taking the cross product of N and (1,1,1), unless N is 1,1,1, in
/// which case we cross with (-1,1,1).  Either way, we get something
/// orthogonal.  Then N x a is mutually orthogonal to the other two.
void
ClosurePrimitive::make_orthonormals (const Vec3 &N, Vec3 &a, Vec3 &b)
{
    if (N[0] != N[1] || N[0] != N[2])
        a = Vec3 (N[2]-N[1], N[0]-N[2], N[1]-N[0]);  // (1,1,1) x N
    else
        a = Vec3 (N[2]-N[1], N[0]+N[2], -N[1]-N[0]);  // (-1,1,1) x N
    a.normalize ();
    b = N.cross (a);
}



void
ClosurePrimitive::sample_cos_hemisphere (const Vec3 &N, const Vec3 &omega_out,
                                         float randu, float randv,
                                         Vec3 &omega_in, float &pdf)
{
    // Default closure BSDF implementation: uniformly sample
    // cosine-weighted hemisphere above the point.
    to_unit_disk (randu, randv);
    float costheta = sqrtf (std::max(1 - randu * randu - randv * randv, 0.0f));
    Vec3 T, B;
    make_orthonormals (N, T, B);
    omega_in = randu * T + randv * B + costheta * N;
    pdf = costheta * (float) M_1_PI;
}



float
ClosurePrimitive::pdf_cos_hemisphere (const Vec3 &N, const Vec3 &omega_in)
{
    // Default closure BSDF implementation: cosine-weighted hemisphere
    // above the point.
    float costheta = N.dot (omega_in);
    return costheta > 0 ? (costheta * (float) M_1_PI) : 0;
}



namespace pvt {


class DiffuseClosure : public BSDFClosure {
public:
    DiffuseClosure () : BSDFClosure ("diffuse", "n") { }

    struct params_t {
        Vec3 N;
    };

    bool get_cone(const void *paramsptr,
                  const Vec3 &omega_out, Vec3 &axis, float &angle) const
    {
        const params_t *params = (const params_t *) paramsptr;
        float cosNO = params->N.dot(omega_out);
        if (cosNO > 0) {
           // we are viewing the surface from the same side as the normal
           axis = params->N;
           angle = (float) M_PI;
           return true;
        }
        // we are below the surface
        return false;
    }

    Color3 eval (const void *paramsptr,
                 const Vec3 &omega_out, const Vec3 &omega_in) const
    {
        const params_t *params = (const params_t *) paramsptr;
        float cos_pi = params->N.dot(omega_in) * (float) M_1_PI;
        return Color3 (cos_pi, cos_pi, cos_pi);
    }

    void sample (const void *paramsptr,
                 const Vec3 &omega_out, float randu, float randv,
                 Vec3 &omega_in, float &pdf) const
    {
        const params_t *params = (const params_t *) paramsptr;
        float cosNO = params->N.dot(omega_out);
        if (cosNO > 0) {
           // we are viewing the surface from above - send a ray out with cosine
           // distribution over the hemisphere
           sample_cos_hemisphere (params->N, omega_out, randu, randv, omega_in, pdf);
        } else {
           // no samples if we look at the surface from the wrong side
           pdf = 0, omega_in.setValue(0.0f, 0.0f, 0.0f);
        }
    }

    float pdf (const void *paramsptr,
               const Vec3 &omega_out, const Vec3 &omega_in) const
    {
        const params_t *params = (const params_t *) paramsptr;
        return pdf_cos_hemisphere (params->N, omega_in);
    }

};


class TransparentClosure : public BSDFClosure {
public:
    TransparentClosure () : BSDFClosure ("transparent", "") { }

    bool get_cone(const void *paramsptr,
                  const Vec3 &omega_out, Vec3 &axis, float &angle) const
    {
        // does not need to be integrated directly
        return false;
    }

    Color3 eval (const void *paramsptr,
                 const Vec3 &omega_out, const Vec3 &omega_in) const
    {
        // should never be called - because get_cone is empty
        return Color3 (0.0f, 0.0f, 0.0f);
    }

    void sample (const void *paramsptr,
                 const Vec3 &omega_out, float randu, float randv,
                 Vec3 &omega_in, float &pdf) const
    {
        // only one direction is possible
        omega_in = -omega_out;
        pdf = 1;
        
    }

    float pdf (const void *paramsptr,
               const Vec3 &omega_out, const Vec3 &omega_in) const
    {
        // the pdf for an arbitrary direction is 0 because only a single
        // direction is actually possible
        return 0;
    }

};

// vanilla phong - leaks energy at grazing angles
// see Global Illumination Compendium entry (66) 
class PhongClosure : public BSDFClosure {
public:
    PhongClosure () : BSDFClosure ("phong", "nf") { }

    struct params_t {
        Vec3 N;
        float exponent;
    };

    bool get_cone(const void *paramsptr,
                  const Vec3 &omega_out, Vec3 &axis, float &angle) const
    {
        const params_t *params = (const params_t *) paramsptr;
        float cosNO = params->N.dot(omega_out);
        if (cosNO > 0) {
            // we are viewing the surface from the same side as the normal
            axis = params->N;
            angle = (float) M_PI;
            return true;
        }
        // we are below the surface
        return false;
    }

    Color3 eval (const void *paramsptr,
                 const Vec3 &omega_out, const Vec3 &omega_in) const
    {
        const params_t *params = (const params_t *) paramsptr;
        float cosNO = params->N.dot(omega_out);
        float cosNI = params->N.dot(omega_in);
        // reflect the view vector
        Vec3 R = (2 * cosNO) * params->N - omega_out;
        float out = cosNI * ((params->exponent + 2) * 0.5f * (float) M_1_PI * powf(R.dot(omega_in), params->exponent));
        return Color3 (out, out, out);
    }

    void sample (const void *paramsptr,
                 const Vec3 &omega_out, float randu, float randv,
                 Vec3 &omega_in, float &pdf) const
    {
        const params_t *params = (const params_t *) paramsptr;
        float cosNO = params->N.dot(omega_out);
        if (cosNO > 0) {
           // reflect the view vector
           Vec3 R = (2 * cosNO) * params->N - omega_out;
           Vec3 T, B;
           make_orthonormals (R, T, B);
           float phi = 2 * (float) M_PI * randu;
           float cosTheta = powf(randv, 1 / (params->exponent + 1));
           float sinTheta = sqrtf(1 - cosTheta * cosTheta);
           omega_in = (cosf(phi) * sinTheta) * T +
                      (sinf(phi) * sinTheta) * B +
                      (            cosTheta) * R;
           // make sure the direction we chose is still in the right hemisphere
           if (params->N.dot(omega_in) > 0) {
              pdf = (params->exponent + 1) * 0.5f * (float) M_1_PI * powf(R.dot(omega_in), params->exponent);
              return;
           }
        }
        pdf = 0, omega_in.setValue(0.0f, 0.0f, 0.0f);
    }

    float pdf (const void *paramsptr,
               const Vec3 &omega_out, const Vec3 &omega_in) const
    {
        const params_t *params = (const params_t *) paramsptr;
        float cosNO = params->N.dot(omega_out);
        Vec3 R = (2 * cosNO) * params->N - omega_out;
        return (params->exponent + 1) * 0.5f * (float) M_1_PI * powf(R.dot(omega_in), params->exponent);
    }

};



// these are all singletons
DiffuseClosure diffuse_closure_primitive;
TransparentClosure transparent_closure_primitive;
PhongClosure phong_closure_primitive;

}; // namespace pvt
}; // namespace OSL
#ifdef OSL_NAMESPACE
}; // end namespace OSL_NAMESPACE
#endif