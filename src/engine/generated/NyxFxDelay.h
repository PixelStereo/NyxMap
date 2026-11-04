/* ------------------------------------------------------------
name: "Delay"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFxDelay -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __NyxFxDelay_H__
#define  __NyxFxDelay_H__

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
#define FAUSTCLASS NyxFxDelay
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


class NyxFxDelay : public dsp {

 private:

	int fSampleRate;
	float fConst0;
	float fConst1;
	FAUSTFLOAT fHslider0;
	float fConst2;
	float fRec1[2];
	int IOTA0;
	float fVec0[524288];
	float fConst3;
	FAUSTFLOAT fHslider1;
	float fRec2[2];
	float fRec3[2];
	float fRec4[2];
	float fRec5[2];
	float fRec0[2];
	float fVec1[524288];
	float fRec6[2];

 public:
	NyxFxDelay() {
	}

	NyxFxDelay(const NyxFxDelay&) = default;

	virtual ~NyxFxDelay() = default;

	NyxFxDelay& operator=(const NyxFxDelay&) = default;

	void metadata(Meta* m) {
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.23.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFxDelay -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("delays.lib/name", "Faust Delay Library");
		m->declare("delays.lib/version", "1.2.0");
		m->declare("filename", "Delay.dsp");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LicenseRef-LGPL-2.1-or-later-with-Faust-exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "Delay");
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
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		fConst1 = 44.1f / fConst0;
		fConst2 = 1.0f - fConst1;
		fConst3 = 0.001f * fConst0;
	}

	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(0.35f);
		fHslider1 = static_cast<FAUSTFLOAT>(2.5e+02f);
	}

	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = faust_wrap_add(l0, 1)) {
			fRec1[l0] = 0.0f;
		}
		IOTA0 = 0;
		for (int l1 = 0; l1 < 524288; l1 = faust_wrap_add(l1, 1)) {
			fVec0[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 2; l2 = faust_wrap_add(l2, 1)) {
			fRec2[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 2; l3 = faust_wrap_add(l3, 1)) {
			fRec3[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 2; l4 = faust_wrap_add(l4, 1)) {
			fRec4[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = faust_wrap_add(l5, 1)) {
			fRec5[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = faust_wrap_add(l6, 1)) {
			fRec0[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 524288; l7 = faust_wrap_add(l7, 1)) {
			fVec1[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 2; l8 = faust_wrap_add(l8, 1)) {
			fRec6[l8] = 0.0f;
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

	virtual NyxFxDelay* clone() {
		return new NyxFxDelay(*this);
	}

	virtual int getSampleRate() {
		return fSampleRate;
	}

	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("Delay");
		ui_interface->addHorizontalSlider("feedback", &fHslider0, FAUSTFLOAT(0.35f), FAUSTFLOAT(0.0f), FAUSTFLOAT(0.9f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("time_ms", &fHslider1, FAUSTFLOAT(2.5e+02f), FAUSTFLOAT(1.0f), FAUSTFLOAT(2e+03f), FAUSTFLOAT(1.0f));
		ui_interface->closeBox();
	}

	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = fConst1 * static_cast<float>(fHslider0);
		float fSlow1 = fConst3 * static_cast<float>(fHslider1);
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			fRec1[0] = fSlow0 + fConst2 * fRec1[1];
			float fTemp0 = static_cast<float>(input0[i0]) + fRec0[1] * fRec1[0];
			fVec0[IOTA0 & 524287] = fTemp0;
			float fTemp1 = ((fRec5[1] != 0.0f) ? (((fRec2[1] > 0.0f) & (fRec2[1] < 1.0f)) ? fRec5[1] : 0.0f) : (((fRec2[1] == 0.0f) & (fSlow1 != fRec3[1])) ? 0.00390625f : (((fRec2[1] == 1.0f) & (fSlow1 != fRec4[1])) ? -0.00390625f : 0.0f)));
			fRec2[0] = std::max<float>(0.0f, std::min<float>(1.0f, fRec2[1] + fTemp1));
			fRec3[0] = (((fRec2[1] >= 1.0f) & (fRec4[1] != fSlow1)) ? fSlow1 : fRec3[1]);
			fRec4[0] = (((fRec2[1] <= 0.0f) & (fRec3[1] != fSlow1)) ? fSlow1 : fRec4[1]);
			fRec5[0] = fTemp1;
			int iTemp2 = static_cast<int>(std::min<float>(3.84e+05f, std::max<float>(0.0f, fRec3[0])));
			float fTemp3 = fVec0[(faust_wrap_sub(IOTA0, iTemp2)) & 524287];
			int iTemp4 = static_cast<int>(std::min<float>(3.84e+05f, std::max<float>(0.0f, fRec4[0])));
			fRec0[0] = fTemp3 + fRec2[0] * (fVec0[(faust_wrap_sub(IOTA0, iTemp4)) & 524287] - fTemp3);
			output0[i0] = static_cast<FAUSTFLOAT>(fRec0[0]);
			float fTemp5 = static_cast<float>(input1[i0]) + fRec1[0] * fRec6[1];
			fVec1[IOTA0 & 524287] = fTemp5;
			float fTemp6 = fVec1[(faust_wrap_sub(IOTA0, iTemp2)) & 524287];
			fRec6[0] = fTemp6 + fRec2[0] * (fVec1[(faust_wrap_sub(IOTA0, iTemp4)) & 524287] - fTemp6);
			output1[i0] = static_cast<FAUSTFLOAT>(fRec6[0]);
			fRec1[1] = fRec1[0];
			IOTA0 = faust_wrap_add(IOTA0, 1);
			fRec2[1] = fRec2[0];
			fRec3[1] = fRec3[0];
			fRec4[1] = fRec4[0];
			fRec5[1] = fRec5[0];
			fRec0[1] = fRec0[0];
			fRec6[1] = fRec6[0];
		}
	}

};

#endif
