/* ------------------------------------------------------------
name: "StereoWidth"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFxStereoWidth -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __NyxFxStereoWidth_H__
#define  __NyxFxStereoWidth_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>
#ifndef FAUST_INT_WRAP
#define FAUST_INT_WRAP
inline int faust_wrap_add(int a, int b) { return int((unsigned int)a + (unsigned int)b); }
inline int faust_wrap_sub(int a, int b) { return int((unsigned int)a - (unsigned int)b); }
inline int faust_wrap_mul(int a, int b) { return int((unsigned int)a * (unsigned int)b); }
#endif


#ifndef FAUSTCLASS
#define FAUSTCLASS NyxFxStereoWidth
#endif

#ifdef __APPLE__
#define exp10f __exp10f
#define exp10 __exp10
#endif

#if defined(_WIN32)
#define RESTRICT __restrict
#else
#define RESTRICT __restrict__
#endif


class NyxFxStereoWidth : public dsp {

 private:

	int fSampleRate;
	float fConst0;
	FAUSTFLOAT fHslider0;
	float fConst1;
	float fRec0[2];

 public:
	NyxFxStereoWidth() {
	}

	NyxFxStereoWidth(const NyxFxStereoWidth&) = default;

	virtual ~NyxFxStereoWidth() = default;

	NyxFxStereoWidth& operator=(const NyxFxStereoWidth&) = default;

	void metadata(Meta* m) {
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFxStereoWidth -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("filename", "StereoWidth.dsp");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LicenseRef-LGPL-2.1-or-later-with-Faust-exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "StereoWidth");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("signals.lib/name", "Faust Routing Library");
		m->declare("signals.lib/version", "1.7.0");
	}

	virtual int getNumInputs() {
		return 2;
	}
	virtual int getNumOutputs() {
		return 2;
	}

	static void classInit(int sample_rate) {
	}

	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = 44.1f / std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		fConst1 = 1.0f - fConst0;
	}

	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(1.0f);
	}

	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = faust_wrap_add(l0, 1)) {
			fRec0[l0] = 0.0f;
		}
	}

	virtual void init(int sample_rate) {
		classInit(sample_rate);
		instanceInit(sample_rate);
	}

	virtual void instanceInit(int sample_rate) {
		instanceConstants(sample_rate);
		instanceResetUserInterface();
		instanceClear();
	}

	virtual NyxFxStereoWidth* clone() {
		return new NyxFxStereoWidth(*this);
	}

	virtual int getSampleRate() {
		return fSampleRate;
	}

	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("StereoWidth");
		ui_interface->addHorizontalSlider("width", &fHslider0, FAUSTFLOAT(1.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(2.0f), FAUSTFLOAT(0.001f));
		ui_interface->closeBox();
	}

	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = fConst0 * static_cast<float>(fHslider0);
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			float fTemp0 = static_cast<float>(input0[i0]);
			float fTemp1 = static_cast<float>(input1[i0]);
			float fTemp2 = fTemp0 + fTemp1;
			fRec0[0] = fSlow0 + fConst1 * fRec0[1];
			float fTemp3 = (fTemp0 - fTemp1) * fRec0[0];
			output0[i0] = static_cast<FAUSTFLOAT>(0.5f * (fTemp2 + fTemp3));
			output1[i0] = static_cast<FAUSTFLOAT>(0.5f * (fTemp2 - fTemp3));
			fRec0[1] = fRec0[0];
		}
	}

};

#endif
