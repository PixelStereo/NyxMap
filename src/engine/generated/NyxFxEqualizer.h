/* ------------------------------------------------------------
name: "Equalizer"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFxEqualizer -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __NyxFxEqualizer_H__
#define  __NyxFxEqualizer_H__

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
#define FAUSTCLASS NyxFxEqualizer
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

static float NyxFxEqualizer_faustpower2_f(float value) {
	return value * value;
}

class NyxFxEqualizer : public dsp {

 private:

	int fSampleRate;
	float fConst0;
	float fConst1;
	float fConst2;
	float fConst3;
	float fConst4;
	float fConst5;
	float fConst6;
	float fConst7;
	float fConst8;
	float fConst9;
	float fConst10;
	float fConst11;
	float fConst12;
	float fVec0[2];
	float fRec4[2];
	float fConst13;
	float fConst14;
	float fConst15;
	float fRec3[3];
	float fConst16;
	FAUSTFLOAT fHslider0;
	float fConst17;
	float fRec5[2];
	float fRec7[2];
	float fRec6[3];
	float fConst18;
	FAUSTFLOAT fHslider1;
	float fRec8[2];
	float fConst19;
	float fRec2[3];
	float fVec1[2];
	float fRec1[2];
	float fConst20;
	float fConst21;
	float fConst22;
	float fRec0[3];
	float fRec10[2];
	float fRec9[3];
	FAUSTFLOAT fHslider2;
	float fRec11[2];
	float fVec2[2];
	float fRec16[2];
	float fRec15[3];
	float fRec18[2];
	float fRec17[3];
	float fRec14[3];
	float fVec3[2];
	float fRec13[2];
	float fRec12[3];
	float fRec20[2];
	float fRec19[3];

 public:
	NyxFxEqualizer() {
	}

	NyxFxEqualizer(const NyxFxEqualizer&) = default;

	virtual ~NyxFxEqualizer() = default;

	NyxFxEqualizer& operator=(const NyxFxEqualizer&) = default;

	void metadata(Meta* m) {
		m->declare("analyzers.lib/name", "Faust Analyzer Library");
		m->declare("analyzers.lib/version", "1.4.0");
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.23.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFxEqualizer -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("filename", "Equalizer.dsp");
		m->declare("filters.lib/filterbank:author", "Julius O. Smith III");
		m->declare("filters.lib/filterbank:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/filterbank:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/fir:author", "Julius O. Smith III");
		m->declare("filters.lib/fir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/fir:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/highpass:author", "Julius O. Smith III");
		m->declare("filters.lib/highpass:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/highpass:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/highshelf:author", "Julius O. Smith III");
		m->declare("filters.lib/highshelf:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/highshelf:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/iir:author", "Julius O. Smith III");
		m->declare("filters.lib/iir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/iir:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/low_shelf:author", "Julius O. Smith III");
		m->declare("filters.lib/low_shelf:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/low_shelf:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/lowpass0_highpass1:author", "Julius O. Smith III");
		m->declare("filters.lib/lowpass0_highpass1:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/lowpass0_highpass1:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/lowpass:author", "Julius O. Smith III");
		m->declare("filters.lib/lowpass:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/lowpass:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/lowshelf:author", "Julius O. Smith III");
		m->declare("filters.lib/lowshelf:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/lowshelf:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/peak_eq:author", "Julius O. Smith III");
		m->declare("filters.lib/peak_eq:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/peak_eq:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/tf1:author", "Julius O. Smith III");
		m->declare("filters.lib/tf1:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf1:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/tf1s:author", "Julius O. Smith III");
		m->declare("filters.lib/tf1s:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf1s:license", "LicenseRef-STK-4.3");
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
		m->declare("name", "Equalizer");
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
		fConst1 = std::tan(3.1415927f * (std::min<float>(5e+03f, 0.4f * fConst0) / fConst0));
		fConst2 = 1.0f / fConst1;
		fConst3 = 1.0f / ((fConst2 + 1.0f) / fConst1 + 1.0f);
		fConst4 = 1.0f / (fConst2 + 1.0f);
		fConst5 = 1.0f - fConst2;
		fConst6 = std::tan(3141.5928f / fConst0);
		fConst7 = 2.0f * (1.0f - 1.0f / NyxFxEqualizer_faustpower2_f(fConst6));
		fConst8 = std::tan(628.31854f / fConst0);
		fConst9 = 1.0f / fConst8;
		fConst10 = 1.0f / ((fConst9 + 1.0f) / fConst8 + 1.0f);
		fConst11 = 1.0f / (fConst9 + 1.0f);
		fConst12 = 1.0f - fConst9;
		fConst13 = (fConst9 + -1.0f) / fConst8 + 1.0f;
		fConst14 = 1.0f / NyxFxEqualizer_faustpower2_f(fConst8);
		fConst15 = 2.0f * (1.0f - fConst14);
		fConst16 = 44.1f / fConst0;
		fConst17 = 1.0f - fConst16;
		fConst18 = 1.0f / fConst6;
		fConst19 = 3141.5928f / (fConst0 * std::sin(6283.1855f / fConst0));
		fConst20 = (fConst2 + -1.0f) / fConst1 + 1.0f;
		fConst21 = 1.0f / NyxFxEqualizer_faustpower2_f(fConst1);
		fConst22 = 2.0f * (1.0f - fConst21);
	}

	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider1 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider2 = static_cast<FAUSTFLOAT>(0.0f);
	}

	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = faust_wrap_add(l0, 1)) {
			fVec0[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 2; l1 = faust_wrap_add(l1, 1)) {
			fRec4[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 3; l2 = faust_wrap_add(l2, 1)) {
			fRec3[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 2; l3 = faust_wrap_add(l3, 1)) {
			fRec5[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 2; l4 = faust_wrap_add(l4, 1)) {
			fRec7[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 3; l5 = faust_wrap_add(l5, 1)) {
			fRec6[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = faust_wrap_add(l6, 1)) {
			fRec8[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 3; l7 = faust_wrap_add(l7, 1)) {
			fRec2[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 2; l8 = faust_wrap_add(l8, 1)) {
			fVec1[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2; l9 = faust_wrap_add(l9, 1)) {
			fRec1[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 3; l10 = faust_wrap_add(l10, 1)) {
			fRec0[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 2; l11 = faust_wrap_add(l11, 1)) {
			fRec10[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 3; l12 = faust_wrap_add(l12, 1)) {
			fRec9[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 2; l13 = faust_wrap_add(l13, 1)) {
			fRec11[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 2; l14 = faust_wrap_add(l14, 1)) {
			fVec2[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 2; l15 = faust_wrap_add(l15, 1)) {
			fRec16[l15] = 0.0f;
		}
		for (int l16 = 0; l16 < 3; l16 = faust_wrap_add(l16, 1)) {
			fRec15[l16] = 0.0f;
		}
		for (int l17 = 0; l17 < 2; l17 = faust_wrap_add(l17, 1)) {
			fRec18[l17] = 0.0f;
		}
		for (int l18 = 0; l18 < 3; l18 = faust_wrap_add(l18, 1)) {
			fRec17[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 3; l19 = faust_wrap_add(l19, 1)) {
			fRec14[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 2; l20 = faust_wrap_add(l20, 1)) {
			fVec3[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 2; l21 = faust_wrap_add(l21, 1)) {
			fRec13[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 3; l22 = faust_wrap_add(l22, 1)) {
			fRec12[l22] = 0.0f;
		}
		for (int l23 = 0; l23 < 2; l23 = faust_wrap_add(l23, 1)) {
			fRec20[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 3; l24 = faust_wrap_add(l24, 1)) {
			fRec19[l24] = 0.0f;
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

	virtual NyxFxEqualizer* clone() {
		return new NyxFxEqualizer(*this);
	}

	virtual int getSampleRate() {
		return fSampleRate;
	}

	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("Equalizer");
		ui_interface->addHorizontalSlider("bass_dB", &fHslider0, FAUSTFLOAT(0.0f), FAUSTFLOAT(-18.0f), FAUSTFLOAT(18.0f), FAUSTFLOAT(0.1f));
		ui_interface->addHorizontalSlider("mid_dB", &fHslider1, FAUSTFLOAT(0.0f), FAUSTFLOAT(-18.0f), FAUSTFLOAT(18.0f), FAUSTFLOAT(0.1f));
		ui_interface->addHorizontalSlider("treble_dB", &fHslider2, FAUSTFLOAT(0.0f), FAUSTFLOAT(-18.0f), FAUSTFLOAT(18.0f), FAUSTFLOAT(0.1f));
		ui_interface->closeBox();
	}

	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = fConst16 * static_cast<float>(fHslider0);
		float fSlow1 = fConst16 * static_cast<float>(fHslider1);
		float fSlow2 = fConst16 * static_cast<float>(fHslider2);
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			float fTemp0 = static_cast<float>(input0[i0]);
			fVec0[0] = fTemp0;
			fRec4[0] = -(fConst11 * (fConst12 * fRec4[1] - (fTemp0 + fVec0[1])));
			fRec3[0] = fRec4[0] - fConst10 * (fConst13 * fRec3[2] + fConst15 * fRec3[1]);
			fRec5[0] = fSlow0 + fConst17 * fRec5[1];
			float fTemp1 = std::pow(1e+01f, 0.05f * fRec5[0]);
			fRec7[0] = -(fConst11 * (fConst12 * fRec7[1] - fConst9 * (fTemp0 - fVec0[1])));
			fRec6[0] = fRec7[0] - fConst10 * (fConst13 * fRec6[2] + fConst15 * fRec6[1]);
			fRec8[0] = fSlow1 + fConst17 * fRec8[1];
			int iTemp2 = fRec8[0] > 0.0f;
			float fTemp3 = fConst19 * std::pow(1e+01f, 0.05f * std::fabs(fRec8[0]));
			float fTemp4 = ((iTemp2) ? fConst19 : fTemp3);
			float fTemp5 = 1.0f + fConst18 * (fConst18 - fTemp4);
			float fTemp6 = fConst7 * fRec2[1];
			float fTemp7 = 1.0f + fConst18 * (fConst18 + fTemp4);
			fRec2[0] = fConst10 * ((fRec3[2] + fRec3[0] + 2.0f * fRec3[1]) * fTemp1 + fConst14 * (fRec6[2] + (fRec6[0] - 2.0f * fRec6[1]))) - (fRec2[2] * fTemp5 + fTemp6) / fTemp7;
			float fTemp8 = ((iTemp2) ? fTemp3 : fConst19);
			float fTemp9 = 1.0f + fConst18 * (fConst18 + fTemp8);
			float fTemp10 = 1.0f + fConst18 * (fConst18 - fTemp8);
			float fTemp11 = (fTemp6 + fRec2[0] * fTemp9 + fRec2[2] * fTemp10) / fTemp7;
			fVec1[0] = fTemp11;
			fRec1[0] = -(fConst4 * (fConst5 * fRec1[1] - (fTemp11 + fVec1[1])));
			fRec0[0] = fRec1[0] - fConst3 * (fConst20 * fRec0[2] + fConst22 * fRec0[1]);
			fRec10[0] = -(fConst4 * (fConst5 * fRec10[1] - fConst2 * (fTemp11 - fVec1[1])));
			fRec9[0] = fRec10[0] - fConst3 * (fConst20 * fRec9[2] + fConst22 * fRec9[1]);
			fRec11[0] = fSlow2 + fConst17 * fRec11[1];
			float fTemp12 = std::pow(1e+01f, 0.05f * fRec11[0]);
			output0[i0] = static_cast<FAUSTFLOAT>(fConst3 * (fRec0[2] + fRec0[0] + 2.0f * fRec0[1] + fConst21 * (fRec9[2] + (fRec9[0] - 2.0f * fRec9[1])) * fTemp12));
			float fTemp13 = static_cast<float>(input1[i0]);
			fVec2[0] = fTemp13;
			fRec16[0] = -(fConst11 * (fConst12 * fRec16[1] - (fTemp13 + fVec2[1])));
			fRec15[0] = fRec16[0] - fConst10 * (fConst13 * fRec15[2] + fConst15 * fRec15[1]);
			fRec18[0] = -(fConst11 * (fConst12 * fRec18[1] - fConst9 * (fTemp13 - fVec2[1])));
			fRec17[0] = fRec18[0] - fConst10 * (fConst13 * fRec17[2] + fConst15 * fRec17[1]);
			float fTemp14 = fConst7 * fRec14[1];
			fRec14[0] = fConst10 * (fTemp1 * (fRec15[2] + fRec15[0] + 2.0f * fRec15[1]) + fConst14 * (fRec17[2] + (fRec17[0] - 2.0f * fRec17[1]))) - (fTemp5 * fRec14[2] + fTemp14) / fTemp7;
			float fTemp15 = (fTemp14 + fTemp9 * fRec14[0] + fTemp10 * fRec14[2]) / fTemp7;
			fVec3[0] = fTemp15;
			fRec13[0] = -(fConst4 * (fConst5 * fRec13[1] - (fTemp15 + fVec3[1])));
			fRec12[0] = fRec13[0] - fConst3 * (fConst20 * fRec12[2] + fConst22 * fRec12[1]);
			fRec20[0] = -(fConst4 * (fConst5 * fRec20[1] - fConst2 * (fTemp15 - fVec3[1])));
			fRec19[0] = fRec20[0] - fConst3 * (fConst20 * fRec19[2] + fConst22 * fRec19[1]);
			output1[i0] = static_cast<FAUSTFLOAT>(fConst3 * (fRec12[2] + fRec12[0] + 2.0f * fRec12[1] + fConst21 * fTemp12 * (fRec19[2] + (fRec19[0] - 2.0f * fRec19[1]))));
			fVec0[1] = fVec0[0];
			fRec4[1] = fRec4[0];
			fRec3[2] = fRec3[1];
			fRec3[1] = fRec3[0];
			fRec5[1] = fRec5[0];
			fRec7[1] = fRec7[0];
			fRec6[2] = fRec6[1];
			fRec6[1] = fRec6[0];
			fRec8[1] = fRec8[0];
			fRec2[2] = fRec2[1];
			fRec2[1] = fRec2[0];
			fVec1[1] = fVec1[0];
			fRec1[1] = fRec1[0];
			fRec0[2] = fRec0[1];
			fRec0[1] = fRec0[0];
			fRec10[1] = fRec10[0];
			fRec9[2] = fRec9[1];
			fRec9[1] = fRec9[0];
			fRec11[1] = fRec11[0];
			fVec2[1] = fVec2[0];
			fRec16[1] = fRec16[0];
			fRec15[2] = fRec15[1];
			fRec15[1] = fRec15[0];
			fRec18[1] = fRec18[0];
			fRec17[2] = fRec17[1];
			fRec17[1] = fRec17[0];
			fRec14[2] = fRec14[1];
			fRec14[1] = fRec14[0];
			fVec3[1] = fVec3[0];
			fRec13[1] = fRec13[0];
			fRec12[2] = fRec12[1];
			fRec12[1] = fRec12[0];
			fRec20[1] = fRec20[0];
			fRec19[2] = fRec19[1];
			fRec19[1] = fRec19[0];
		}
	}

};

#endif
