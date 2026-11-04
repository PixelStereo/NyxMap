/* ------------------------------------------------------------
name: "HighPass"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFxHighPass -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __NyxFxHighPass_H__
#define  __NyxFxHighPass_H__

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
#define FAUSTCLASS NyxFxHighPass
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

static float NyxFxHighPass_faustpower2_f(float value) {
	return value * value;
}

class NyxFxHighPass : public dsp {

 private:

	int fSampleRate;
	float fConst0;
	float fConst1;
	float fConst2;
	FAUSTFLOAT fHslider0;
	float fConst3;
	float fRec1[2];
	float fConst4;
	FAUSTFLOAT fHslider1;
	float fRec2[2];
	float fRec0[3];
	float fRec3[3];

 public:
	NyxFxHighPass() {
	}

	NyxFxHighPass(const NyxFxHighPass&) = default;

	virtual ~NyxFxHighPass() = default;

	NyxFxHighPass& operator=(const NyxFxHighPass&) = default;

	void metadata(Meta* m) {
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFxHighPass -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("filename", "HighPass.dsp");
		m->declare("filters.lib/fir:author", "Julius O. Smith III");
		m->declare("filters.lib/fir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/fir:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/iir:author", "Julius O. Smith III");
		m->declare("filters.lib/iir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/iir:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/resonhp:author", "Julius O. Smith III");
		m->declare("filters.lib/resonhp:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/resonhp:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/resonlp:author", "Julius O. Smith III");
		m->declare("filters.lib/resonlp:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/resonlp:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/tf2:author", "Julius O. Smith III");
		m->declare("filters.lib/tf2:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf2:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/tf2s:author", "Julius O. Smith III");
		m->declare("filters.lib/tf2s:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf2s:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/version", "1.9.0");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LicenseRef-LGPL-2.1-or-later-with-Faust-exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "HighPass");
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
		fConst1 = 3.1415927f / fConst0;
		fConst2 = 44.1f / fConst0;
		fConst3 = 1.0f - fConst2;
		fConst4 = 0.45f * fConst0;
	}

	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(2e+02f);
		fHslider1 = static_cast<FAUSTFLOAT>(0.707f);
	}

	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = faust_wrap_add(l0, 1)) {
			fRec1[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 2; l1 = faust_wrap_add(l1, 1)) {
			fRec2[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 3; l2 = faust_wrap_add(l2, 1)) {
			fRec0[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 3; l3 = faust_wrap_add(l3, 1)) {
			fRec3[l3] = 0.0f;
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

	virtual NyxFxHighPass* clone() {
		return new NyxFxHighPass(*this);
	}

	virtual int getSampleRate() {
		return fSampleRate;
	}

	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("HighPass");
		ui_interface->addHorizontalSlider("cutoff", &fHslider0, FAUSTFLOAT(2e+02f), FAUSTFLOAT(2e+01f), FAUSTFLOAT(2e+04f), FAUSTFLOAT(1.0f));
		ui_interface->addHorizontalSlider("resonance", &fHslider1, FAUSTFLOAT(0.707f), FAUSTFLOAT(0.1f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->closeBox();
	}

	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = fConst2 * static_cast<float>(fHslider0);
		float fSlow1 = fConst2 * static_cast<float>(fHslider1);
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			float fTemp0 = static_cast<float>(input0[i0]);
			fRec1[0] = fSlow0 + fConst3 * fRec1[1];
			float fTemp1 = std::tan(fConst1 * std::min<float>(fRec1[0], fConst4));
			float fTemp2 = 1.0f / fTemp1;
			fRec2[0] = fSlow1 + fConst3 * fRec2[1];
			float fTemp3 = 1.0f / fRec2[0];
			float fTemp4 = 1.0f + (fTemp2 - fTemp3) / fTemp1;
			float fTemp5 = 1.0f - 1.0f / NyxFxHighPass_faustpower2_f(fTemp1);
			float fTemp6 = 1.0f + (fTemp2 + fTemp3) / fTemp1;
			fRec0[0] = fTemp0 - (fRec0[2] * fTemp4 + 2.0f * fRec0[1] * fTemp5) / fTemp6;
			output0[i0] = static_cast<FAUSTFLOAT>(fTemp0 - (fRec0[2] + fRec0[0] + 2.0f * fRec0[1]) / fTemp6);
			float fTemp7 = static_cast<float>(input1[i0]);
			fRec3[0] = fTemp7 - (fTemp4 * fRec3[2] + 2.0f * fTemp5 * fRec3[1]) / fTemp6;
			output1[i0] = static_cast<FAUSTFLOAT>(fTemp7 - (fRec3[2] + fRec3[0] + 2.0f * fRec3[1]) / fTemp6);
			fRec1[1] = fRec1[0];
			fRec2[1] = fRec2[0];
			fRec0[2] = fRec0[1];
			fRec0[1] = fRec0[0];
			fRec3[2] = fRec3[1];
			fRec3[1] = fRec3[0];
		}
	}

};

#endif
