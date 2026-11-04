/* ------------------------------------------------------------
name: "Distortion"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFxDistortion -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __NyxFxDistortion_H__
#define  __NyxFxDistortion_H__

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
#define FAUSTCLASS NyxFxDistortion
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

static float NyxFxDistortion_faustpower2_f(float value) {
	return value * value;
}

class NyxFxDistortion : public dsp {

 private:

	int fSampleRate;
	float fConst0;
	float fConst1;
	FAUSTFLOAT fHslider0;
	float fConst2;
	float fRec2[2];
	float fVec0[2];
	float fRec1[2];
	float fConst3;
	FAUSTFLOAT fHslider1;
	float fRec3[2];
	float fConst4;
	float fRec0[3];
	float fVec1[2];
	float fRec5[2];
	float fRec4[3];

 public:
	NyxFxDistortion() {
	}

	NyxFxDistortion(const NyxFxDistortion&) = default;

	virtual ~NyxFxDistortion() = default;

	NyxFxDistortion& operator=(const NyxFxDistortion&) = default;

	void metadata(Meta* m) {
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFxDistortion -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("filename", "Distortion.dsp");
		m->declare("filters.lib/dcblocker:author", "Julius O. Smith III");
		m->declare("filters.lib/dcblocker:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/dcblocker:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/fir:author", "Julius O. Smith III");
		m->declare("filters.lib/fir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/fir:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/iir:author", "Julius O. Smith III");
		m->declare("filters.lib/iir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/iir:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/lowpass0_highpass1:author", "Julius O. Smith III");
		m->declare("filters.lib/lowpass0_highpass1:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/lowpass0_highpass1:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/lowpass:author", "Julius O. Smith III");
		m->declare("filters.lib/lowpass:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/lowpass:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/pole:author", "Julius O. Smith III");
		m->declare("filters.lib/pole:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/pole:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/tf2:author", "Julius O. Smith III");
		m->declare("filters.lib/tf2:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf2:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/tf2s:author", "Julius O. Smith III");
		m->declare("filters.lib/tf2s:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf2s:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/version", "1.9.0");
		m->declare("filters.lib/zero:author", "Julius O. Smith III");
		m->declare("filters.lib/zero:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/zero:license", "LicenseRef-STK-4.3");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LicenseRef-LGPL-2.1-or-later-with-Faust-exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("misceffects.lib/cubicnl:author", "Julius O. Smith III");
		m->declare("misceffects.lib/cubicnl:license", "LicenseRef-STK-4.3");
		m->declare("misceffects.lib/cubicnl_nodc:author", "Julius O. Smith III");
		m->declare("misceffects.lib/cubicnl_nodc:license", "LicenseRef-STK-4.3");
		m->declare("misceffects.lib/name", "Misc Effects Library");
		m->declare("misceffects.lib/version", "2.6.0");
		m->declare("name", "Distortion");
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
		fConst3 = 3.1415927f / fConst0;
		fConst4 = 0.45f * fConst0;
	}

	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(0.5f);
		fHslider1 = static_cast<FAUSTFLOAT>(6e+03f);
	}

	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = faust_wrap_add(l0, 1)) {
			fRec2[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 2; l1 = faust_wrap_add(l1, 1)) {
			fVec0[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 2; l2 = faust_wrap_add(l2, 1)) {
			fRec1[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 2; l3 = faust_wrap_add(l3, 1)) {
			fRec3[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 3; l4 = faust_wrap_add(l4, 1)) {
			fRec0[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = faust_wrap_add(l5, 1)) {
			fVec1[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = faust_wrap_add(l6, 1)) {
			fRec5[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 3; l7 = faust_wrap_add(l7, 1)) {
			fRec4[l7] = 0.0f;
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

	virtual NyxFxDistortion* clone() {
		return new NyxFxDistortion(*this);
	}

	virtual int getSampleRate() {
		return fSampleRate;
	}

	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("Distortion");
		ui_interface->addHorizontalSlider("drive", &fHslider0, FAUSTFLOAT(0.5f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("tone", &fHslider1, FAUSTFLOAT(6e+03f), FAUSTFLOAT(5e+01f), FAUSTFLOAT(2e+04f), FAUSTFLOAT(1.0f));
		ui_interface->closeBox();
	}

	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = fConst1 * static_cast<float>(fHslider0);
		float fSlow1 = fConst1 * static_cast<float>(fHslider1);
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			fRec2[0] = fSlow0 + fConst2 * fRec2[1];
			float fTemp0 = std::pow(1e+01f, 2.0f * fRec2[0]);
			float fTemp1 = std::max<float>(-1.0f, std::min<float>(1.0f, static_cast<float>(input0[i0]) * fTemp0));
			float fTemp2 = fTemp1 * (1.0f - 0.33333334f * NyxFxDistortion_faustpower2_f(fTemp1));
			fVec0[0] = fTemp2;
			fRec1[0] = 0.995f * fRec1[1] + fTemp2 - fVec0[1];
			fRec3[0] = fSlow1 + fConst2 * fRec3[1];
			float fTemp3 = std::tan(fConst3 * std::min<float>(fRec3[0], fConst4));
			float fTemp4 = 1.0f / fTemp3;
			float fTemp5 = 1.0f + (-1.4142135f + fTemp4) / fTemp3;
			float fTemp6 = 1.0f - 1.0f / NyxFxDistortion_faustpower2_f(fTemp3);
			float fTemp7 = 1.0f + (1.4142135f + fTemp4) / fTemp3;
			fRec0[0] = fRec1[0] - (fRec0[2] * fTemp5 + 2.0f * fRec0[1] * fTemp6) / fTemp7;
			output0[i0] = static_cast<FAUSTFLOAT>((fRec0[2] + fRec0[0] + 2.0f * fRec0[1]) / fTemp7);
			float fTemp8 = std::max<float>(-1.0f, std::min<float>(1.0f, static_cast<float>(input1[i0]) * fTemp0));
			float fTemp9 = fTemp8 * (1.0f - 0.33333334f * NyxFxDistortion_faustpower2_f(fTemp8));
			fVec1[0] = fTemp9;
			fRec5[0] = 0.995f * fRec5[1] + fTemp9 - fVec1[1];
			fRec4[0] = fRec5[0] - (fTemp5 * fRec4[2] + 2.0f * fTemp6 * fRec4[1]) / fTemp7;
			output1[i0] = static_cast<FAUSTFLOAT>((fRec4[2] + fRec4[0] + 2.0f * fRec4[1]) / fTemp7);
			fRec2[1] = fRec2[0];
			fVec0[1] = fVec0[0];
			fRec1[1] = fRec1[0];
			fRec3[1] = fRec3[0];
			fRec0[2] = fRec0[1];
			fRec0[1] = fRec0[0];
			fVec1[1] = fVec1[0];
			fRec5[1] = fRec5[0];
			fRec4[2] = fRec4[1];
			fRec4[1] = fRec4[0];
		}
	}

};

#endif
