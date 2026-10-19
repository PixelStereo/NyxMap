#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <QString>

// Independent double-precision colorimetric oracle. Derive matrices from the
// published D65/chromaticity coordinates, not the shader's matrix constants.
// https://www.w3.org/TR/css-color-4/#color-conversion-code
// Gamma 2.4 is the ideal-black BT.1886 approximation, not the camera BT.709 OETF.
namespace colorReference {
using Vector = std::array<double, 3>;
using Matrix = std::array<Vector, 3>;
inline Vector multiply(const Matrix &m, const Vector &v) {
    Vector result{};
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) result[i]+=m[i][j]*v[j];
    return result;
}
inline Vector solve(const Matrix &m, const Vector &v) {
    double a[3][4]{};
    for(int i=0;i<3;++i) { for(int j=0;j<3;++j) a[i][j]=m[i][j]; a[i][3]=v[i]; }
    for(int k=0;k<3;++k) {
        int pivot=k;
        for(int i=k+1;i<3;++i) if(std::abs(a[i][k])>std::abs(a[pivot][k])) pivot=i;
        for(int j=0;j<4;++j) std::swap(a[k][j],a[pivot][j]);
        const double scale=a[k][k];
        for(int j=k;j<4;++j) a[k][j]/=scale;
        for(int i=0;i<3;++i) if(i!=k) {
            const double factor=a[i][k];
            for(int j=k;j<4;++j) a[i][j]-=factor*a[k][j];
        }
    }
    return {a[0][3],a[1][3],a[2][3]};
}
inline Matrix rgbToXyz(bool p3) {
    const double xy[3][2]={{p3?.68:.64,p3?.32:.33}, {p3?.265:.30,p3?.69:.60}, {.15,.06}};
    Matrix m{};
    for(int c=0;c<3;++c) {
        m[0][c]=xy[c][0]/xy[c][1];
        m[1][c]=1;
        m[2][c]=(1-xy[c][0]-xy[c][1])/xy[c][1];
    }
    const auto scales=solve(m,{.3127/.3290,1,(1-.3127-.3290)/.3290});
    for(int c=0;c<3;++c) for(int r=0;r<3;++r) m[r][c]*=scales[c];
    return m;
}
inline Vector convert(Vector rgb, const QString &input, const QString &output) {
    for(double &c:rgb) {
        if(input=="rec709-g24") c=std::pow(c,2.4);
        else if(input=="rec709") c=c<.081?c/4.5:std::pow((c+.099)/1.099,1/.45);
        else if(input!="linear709") c=c<=.04045?c/12.92:std::pow((c+.055)/1.055,2.4);
    }
    rgb=solve(rgbToXyz(output=="displayp3"),multiply(rgbToXyz(input=="displayp3"),rgb));
    for(double &c:rgb) {
        c=std::max(0.,c);
        if(output=="rec709-g24") c=std::pow(c,1/2.4);
        else if(output!="linear709") c=c<=.0031308?12.92*c:1.055*std::pow(c,1/2.4)-.055;
        c=std::clamp(c,0.,1.);
    }
    return rgb;
}
}
