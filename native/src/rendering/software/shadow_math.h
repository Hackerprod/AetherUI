#pragma once
#include <cmath>
namespace aether::rendering::software::shadow_math {
// GL16 nodes and weights fixed before precision-corpus execution.
inline double integral(double lo,double hi,double a,double b,double sigma) noexcept {
 if(hi<=lo || b<=a) return 0;
 if(sigma==0) return std::fmax(0.0,std::fmin(hi,b)-std::fmax(lo,a));
 constexpr double nodes[]{.095012509837637440185,.28160355077925891323,.45801677765722738634,.61787624440264374845,.75540440835500303390,.86563120238783174388,.94457502307323257608,.98940093499164993260};
 constexpr double weights[]{.18945061045506849629,.18260341504492358887,.16915651939500253819,.14959598881657673208,.12462897125553387205,.09515851168249278481,.06225352393864789286,.02715245941175409485};
 const double mid=(lo+hi)/2,half=(hi-lo)/2,den=sigma*std::sqrt(2.0);
 const auto value=[&](double x){
  const double u=(x-a)/den,v=(x-b)/den;
  // Phi(u)-Phi(v), evaluated in the appropriate tail.
  if(v>=0) return .5*(std::erfc(v)-std::erfc(u));
  if(u<=0) return .5*(std::erfc(-u)-std::erfc(-v));
  return 1-.5*(std::erfc(u)+std::erfc(-v));
 };
 double sum=0;for(int i=0;i<8;++i)sum+=weights[i]*(value(mid-half*nodes[i])+value(mid+half*nodes[i]));
 return half*sum;
}
inline bool bounded(double value,double upper) noexcept {
 return std::isfinite(value) && value>=-1e-12 && value<=upper+1e-12;
}
}
