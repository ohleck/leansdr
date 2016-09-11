#ifndef LEANSDR_DSP_H
#define LEANSDR_DSP_H

#include <math.h>

namespace leansdr {

  //////////////////////////////////////////////////////////////////////
  // DSP blocks
  //////////////////////////////////////////////////////////////////////
  
  template<typename T>
  T min(const T &x, const T &y) { return (x<y) ? x : y; }

  template<typename T>
  struct complex {
    T re, im;
    complex() { }
    complex(T x) : re(x), im(0) { }
    complex(T x, T y) : re(x), im(y) { }
  };

  template<typename T>
  complex<T> operator +(const complex<T> &a, const complex<T> &b) {
    return complex<T>(a.re+b.re, a.im+b.im);
  }

  template<typename T>
  complex<T> operator *(const complex<T> &a, const T &k) {
    return complex<T>(a.re*k, a.im*k);
  }

  // [cconverter] converts complex streams between numric types,
  // with optionnal ofsetting and rational scaling.
  template<typename Tin, int Zin, typename Tout, int Zout, int Gn, int Gd>
  struct cconverter : runnable {
    pipereader< complex<Tin> > in;
    pipewriter< complex<Tout> > out;
    cconverter(scheduler *sch, pipebuf< complex<Tin> > &_in,
	       pipebuf< complex<Tout> > &_out) 
      : runnable(sch, "cconverter"),
	in(_in), out(_out) {
    }
    void run() {
      unsigned long count = min(in.readable(), out.writable());
      complex<Tin> *pin=in.rd(), *pend=pin+count;
      complex<Tout> *pout = out.wr();
      for ( ; pin<pend; ++pin,++pout ) {
	pout->re = Zout + ((Tout)pin->re-(Tout)Zin)*Gn/Gd;
	pout->im = Zout + ((Tout)pin->im-(Tout)Zin)*Gn/Gd;
      }
      in.read(count);
      out.written(count);
    }
  };
  
  template<typename T>
  struct cfft_engine {
    const int n;
    cfft_engine(int _n) : n(_n), invsqrtn(1/sqrt(n)) {
      // Compute log2(n)
      logn = 0;
      for ( int t=n; t>1; t>>=1 ) ++logn;
      // Bit reversal
      bitrev = new int[n];    
      for ( int i=0; i<n; ++i ) {
	bitrev[i] = 0;
	for ( int b=0; b<logn; ++b ) bitrev[i] = (bitrev[i]<<1) | ((i>>b)&1);
      }
      // Float constants
      omega = new complex<T>[n];
      omega_rev = new complex<T>[n];
      for ( int i=0; i<n; ++i ) {
	float a = 2.0*M_PI * i / n;
	omega_rev[i].re =   (omega[i].re = cosf(a));
	omega_rev[i].im = - (omega[i].im = sinf(a));
      }
    }
    void inplace(complex<T> *data, bool reverse=false) {
      // Bit-reversal permutation
      for ( int i=0; i<n; ++i ) {
	int r = bitrev[i];
	if ( r < i ) { complex<T> tmp=data[i]; data[i]=data[r]; data[r]=tmp; }
      }
      complex<T> *om = reverse ? omega_rev : omega;
      // Danielson-Lanczos
      for ( int i=0; i<logn; ++i ) {
	int hbs = 1 << i;
	int dom = 1 << (logn-1-i);
	for ( int j=0; j<dom; ++j ) {
	  int p = j*hbs*2, q = p+hbs;
	  for ( int k=0; k<hbs; ++k ) {
	    complex<T> &w = om[k*dom];
	    complex<T> &dqk = data[q+k];
	    complex<T> x(w.re*dqk.re - w.im*dqk.im,
			 w.re*dqk.im + w.im*dqk.re);
	    data[q+k].re = data[p+k].re - x.re;
	    data[q+k].im = data[p+k].im - x.im;
	    data[p+k].re = data[p+k].re + x.re;
	    data[p+k].im = data[p+k].im + x.im;
	  }
	}
      }
      float invn = 1.0 / n;
      for ( int i=0; i<n; ++i ) {
	data[i].re *= invn;
	data[i].im *= invn;
      }
    }
  private:
    int logn;
    int *bitrev;
    complex<float> *omega, *omega_rev;
    float invsqrtn;
  };
  
  template<typename T>
  struct adder : runnable {
    adder(scheduler *sch,
	  pipebuf<T> &_in1, pipebuf<T> &_in2, pipebuf<T> &_out)
      : runnable(sch, "adder"),
	in1(_in1), in2(_in2), out(_out) {
    }
    void run() {
      int n = out.writable();
      if ( in1.readable() < n ) n = in1.readable();
      if ( in2.readable() < n ) n = in2.readable();
      T *pin1=in1.rd(), *pin2=in2.rd(), *pout=out.wr(), *pend=pout+n;
      while ( pout < pend ) *pout++ = *pin1++ + *pin2++;
      in1.read(n);
      in2.read(n);
      out.written(n);
    }
  private:
    pipereader<T> in1, in2;
    pipewriter<T> out;
  };
  
  // [awgb_c] generates complex white gaussian noise.
  
  template<typename T>
  struct wgn_c : runnable {
    wgn_c(scheduler *sch, pipebuf< complex<T> > &_out)
      : runnable(sch, "awgn"), stddev(1.0), out(_out) {
    }
    void run() {
      int n = out.writable();
      complex<T> *pout=out.wr(), *pend=pout+n;
      while ( pout < pend ) {
	float x, y, r2;
	do {
	  x = 2*drand48() - 1;
	  y = 2*drand48() - 1;
	  r2 = x*x + y*y;
	} while ( r2==0 || r2>=1 );
	float k = sqrtf(-2*log(r2)/r2) * stddev;
	pout->re = k*x;
	pout->im = k*y;
	++pout;
      }
      out.written(n);
    }
    float stddev;
  private:
    pipewriter< complex<T> > out;
  };
  
  template<typename T>
  struct naive_lowpass : runnable {
    naive_lowpass(scheduler *sch, pipebuf<T> &_in, pipebuf<T> &_out, int _w)
      : runnable(sch, "lowpass"), in(_in), out(_out), w(_w) {
    }
    
    void run() {
      if ( in.readable() < w ) return;
      unsigned long count = min(in.readable()-w, out.writable());
      T *pin=in.rd(), *pend=pin+count;
      T *pout = out.wr();
      float k = 1.0 / w;
      for ( ; pin<pend; ++pin,++pout ) {
	T x = 0.0;
	for ( int i=0; i<w; ++i ) x = x + pin[i];
	*pout = x * k;
      }
      in.read(count);
      out.written(count);
    }
    
  private:
    int w;
    pipereader<T> in;
    pipewriter<T> out;
  };

}  // namespace

#endif  // LEANSDR_DSP_H