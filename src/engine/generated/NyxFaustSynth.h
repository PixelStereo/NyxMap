/* ------------------------------------------------------------
name: "NyxSynth"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFaustSynth -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __NyxFaustSynth_H__
#define  __NyxFaustSynth_H__

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
#define FAUSTCLASS NyxFaustSynth
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

class NyxFaustSynthSIG0 {

  private:

	int iVec0[2];
	int iRec0[2];
	int fSampleRate;

  public:

	int getNumInputsNyxFaustSynthSIG0() {
		return 0;
	}
	int getNumOutputsNyxFaustSynthSIG0() {
		return 1;
	}

	void instanceInitNyxFaustSynthSIG0(int sample_rate) {
		fSampleRate = sample_rate;
		for (int l0 = 0; l0 < 2; l0 = faust_wrap_add(l0, 1)) {
			iVec0[l0] = 0;
		}
		for (int l1 = 0; l1 < 2; l1 = faust_wrap_add(l1, 1)) {
			iRec0[l1] = 0;
		}
	}

	void fillNyxFaustSynthSIG0(int count, float* table) {
		for (int i1 = 0; i1 < count; i1 = faust_wrap_add(i1, 1)) {
			iVec0[0] = 1;
			iRec0[0] = (faust_wrap_add(iVec0[1], iRec0[1])) % 65536;
			table[i1] = std::sin(9.58738e-05f * static_cast<float>(iRec0[0]));
			iVec0[1] = iVec0[0];
			iRec0[1] = iRec0[0];
		}
	}

};

static NyxFaustSynthSIG0* newNyxFaustSynthSIG0() { return (NyxFaustSynthSIG0*)new NyxFaustSynthSIG0(); }
static void deleteNyxFaustSynthSIG0(NyxFaustSynthSIG0* dsp) { delete dsp; }

static float ftbl0NyxFaustSynthSIG0[65536];
static float NyxFaustSynth_faustpower2_f(float value) {
	return value * value;
}

class NyxFaustSynth : public dsp {

 private:

	FAUSTFLOAT fEntry0;
	int iVec1[2];
	int fSampleRate;
	float fConst0;
	float fConst1;
	float fConst2;
	FAUSTFLOAT fHslider0;
	float fConst3;
	float fRec2[2];
	FAUSTFLOAT fHslider1;
	float fRec3[2];
	FAUSTFLOAT fHslider2;
	float fRec4[2];
	float fRec5[2];
	FAUSTFLOAT fHslider3;
	float fRec6[2];
	float fRec1[2];
	FAUSTFLOAT fCheckbox0;
	float fRec7[2];
	int IOTA0;
	float fVec2[131072];
	FAUSTFLOAT fHslider4;
	float fRec9[2];
	float fConst4;
	FAUSTFLOAT fHslider5;
	float fRec10[2];
	float fRec8[2];
	float fConst5;
	FAUSTFLOAT fHslider6;
	float fRec12[2];
	float fRec11[2];
	float fRec14[2];
	float fRec15[2];
	float fRec16[2];
	float fRec17[2];
	float fRec13[2];
	int iRec18[2];
	float fRec19[4];
	FAUSTFLOAT fHslider7;
	float fRec20[2];
	FAUSTFLOAT fEntry1;
	FAUSTFLOAT fHslider8;
	FAUSTFLOAT fCheckbox1;
	float fVec3[2];
	float fRec21[2];
	FAUSTFLOAT fHslider9;
	FAUSTFLOAT fHslider10;
	FAUSTFLOAT fHslider11;
	int iVec4[2];
	int iRec22[2];
	int iVec5[2];
	int iRec24[2];
	float fRec23[2];
	int iRec28[2];
	FAUSTFLOAT fHslider12;
	float fRec25[2];
	int iRec26[2];
	FAUSTFLOAT fHslider13;
	FAUSTFLOAT fHslider14;
	FAUSTFLOAT fHslider15;
	float fRec27[2];
	FAUSTFLOAT fHslider16;
	int iRec29[2];
	float fRec30[2];
	float fRec31[2];
	FAUSTFLOAT fHslider17;
	float fRec32[2];
	int iRec33[2];
	int iRec34[2];
	float fRec35[2];
	float fRec36[2];
	float fRec37[2];
	int iRec38[2];
	float fRec39[2];
	int iRec40[2];

 public:
	NyxFaustSynth() {
	}

	NyxFaustSynth(const NyxFaustSynth&) = default;

	virtual ~NyxFaustSynth() = default;

	NyxFaustSynth& operator=(const NyxFaustSynth&) = default;

	void metadata(Meta* m) {
		m->declare("aanl.lib/clip:deprecated", "internal helper, renamed _clip");
		m->declare("aanl.lib/name", "Faust Antialiased Nonlinearities");
		m->declare("aanl.lib/version", "1.5.0");
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.23.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFaustSynth -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("delays.lib/name", "Faust Delay Library");
		m->declare("delays.lib/version", "1.2.0");
		m->declare("envelopes.lib/adsr:author", "Yann Orlarey and Andrey Bundin");
		m->declare("envelopes.lib/adsr_bias:author", "Andrew John March and David Braun");
		m->declare("envelopes.lib/adsr_bias:licence", "LicenseRef-STK-4.3");
		m->declare("envelopes.lib/adsre:author", "Julius O. Smith III");
		m->declare("envelopes.lib/adsre:licence", "LicenseRef-STK-4.3");
		m->declare("envelopes.lib/adsrf_bias:author", "Andrew John March and David Braun");
		m->declare("envelopes.lib/adsrf_bias:licence", "LicenseRef-STK-4.3");
		m->declare("envelopes.lib/ahdsr_bias:author", "Andrew John March and David Braun");
		m->declare("envelopes.lib/ahdsr_bias:licence", "LicenseRef-STK-4.3");
		m->declare("envelopes.lib/ahdsre:author", "Julius O. Smith III, David Braun");
		m->declare("envelopes.lib/ahdsre:licence", "LicenseRef-STK-4.3");
		m->declare("envelopes.lib/ahdsrf_bias:author", "Andrew John March and David Braun");
		m->declare("envelopes.lib/ahdsrf_bias:licence", "LicenseRef-STK-4.3");
		m->declare("envelopes.lib/author", "GRAME");
		m->declare("envelopes.lib/copyright", "GRAME");
		m->declare("envelopes.lib/license", "LicenseRef-LGPL-2.1-or-later-with-Faust-exception");
		m->declare("envelopes.lib/name", "Faust Envelope Library");
		m->declare("envelopes.lib/version", "1.3.0");
		m->declare("filename", "NyxSynth.dsp");
		m->declare("filters.lib/fir:author", "Julius O. Smith III");
		m->declare("filters.lib/fir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/fir:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/iir:author", "Julius O. Smith III");
		m->declare("filters.lib/iir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/iir:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/version", "1.9.0");
		m->declare("interpolators.lib/name", "Faust Interpolator Library");
		m->declare("interpolators.lib/remap:author", "David Braun");
		m->declare("interpolators.lib/version", "1.6.0");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LicenseRef-LGPL-2.1-or-later-with-Faust-exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("misceffects.lib/name", "Misc Effects Library");
		m->declare("misceffects.lib/version", "2.6.0");
		m->declare("name", "NyxSynth");
		m->declare("noises.lib/name", "Faust Noise Generator Library");
		m->declare("noises.lib/version", "1.6.0");
		m->declare("oscillators.lib/name", "Faust Oscillator Library");
		m->declare("oscillators.lib/version", "1.8.0");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("signals.lib/name", "Faust Routing Library");
		m->declare("signals.lib/version", "1.7.0");
	}

	virtual int getNumInputs() {
		return 4;
	}
	virtual int getNumOutputs() {
		return 1;
	}

	static void classInit(int sample_rate) {
		NyxFaustSynthSIG0* sig0 = newNyxFaustSynthSIG0();
		sig0->instanceInitNyxFaustSynthSIG0(sample_rate);
		sig0->fillNyxFaustSynthSIG0(65536, ftbl0NyxFaustSynthSIG0);
		deleteNyxFaustSynthSIG0(sig0);
	}

	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		fConst1 = 1.0f / fConst0;
		fConst2 = 44.1f / fConst0;
		fConst3 = 1.0f - fConst2;
		fConst4 = 0.001f * fConst0;
		fConst5 = 0.0005f * fConst0;
	}

	virtual void instanceResetUserInterface() {
		fEntry0 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider0 = static_cast<FAUSTFLOAT>(2.2e+02f);
		fHslider1 = static_cast<FAUSTFLOAT>(1.0f);
		fHslider2 = static_cast<FAUSTFLOAT>(2.0f);
		fHslider3 = static_cast<FAUSTFLOAT>(2.0f);
		fCheckbox0 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider4 = static_cast<FAUSTFLOAT>(1.0f);
		fHslider5 = static_cast<FAUSTFLOAT>(8e+01f);
		fHslider6 = static_cast<FAUSTFLOAT>(15.0f);
		fHslider7 = static_cast<FAUSTFLOAT>(0.2f);
		fEntry1 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider8 = static_cast<FAUSTFLOAT>(0.01f);
		fCheckbox1 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider9 = static_cast<FAUSTFLOAT>(0.8f);
		fHslider10 = static_cast<FAUSTFLOAT>(0.1f);
		fHslider11 = static_cast<FAUSTFLOAT>(0.1f);
		fHslider12 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider13 = static_cast<FAUSTFLOAT>(0.5f);
		fHslider14 = static_cast<FAUSTFLOAT>(0.5f);
		fHslider15 = static_cast<FAUSTFLOAT>(0.5f);
		fHslider16 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider17 = static_cast<FAUSTFLOAT>(0.0f);
	}

	virtual void instanceClear() {
		for (int l2 = 0; l2 < 2; l2 = faust_wrap_add(l2, 1)) {
			iVec1[l2] = 0;
		}
		for (int l3 = 0; l3 < 2; l3 = faust_wrap_add(l3, 1)) {
			fRec2[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 2; l4 = faust_wrap_add(l4, 1)) {
			fRec3[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = faust_wrap_add(l5, 1)) {
			fRec4[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = faust_wrap_add(l6, 1)) {
			fRec5[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 2; l7 = faust_wrap_add(l7, 1)) {
			fRec6[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 2; l8 = faust_wrap_add(l8, 1)) {
			fRec1[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2; l9 = faust_wrap_add(l9, 1)) {
			fRec7[l9] = 0.0f;
		}
		IOTA0 = 0;
		for (int l10 = 0; l10 < 131072; l10 = faust_wrap_add(l10, 1)) {
			fVec2[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 2; l11 = faust_wrap_add(l11, 1)) {
			fRec9[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 2; l12 = faust_wrap_add(l12, 1)) {
			fRec10[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 2; l13 = faust_wrap_add(l13, 1)) {
			fRec8[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 2; l14 = faust_wrap_add(l14, 1)) {
			fRec12[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 2; l15 = faust_wrap_add(l15, 1)) {
			fRec11[l15] = 0.0f;
		}
		for (int l16 = 0; l16 < 2; l16 = faust_wrap_add(l16, 1)) {
			fRec14[l16] = 0.0f;
		}
		for (int l17 = 0; l17 < 2; l17 = faust_wrap_add(l17, 1)) {
			fRec15[l17] = 0.0f;
		}
		for (int l18 = 0; l18 < 2; l18 = faust_wrap_add(l18, 1)) {
			fRec16[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 2; l19 = faust_wrap_add(l19, 1)) {
			fRec17[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 2; l20 = faust_wrap_add(l20, 1)) {
			fRec13[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 2; l21 = faust_wrap_add(l21, 1)) {
			iRec18[l21] = 0;
		}
		for (int l22 = 0; l22 < 4; l22 = faust_wrap_add(l22, 1)) {
			fRec19[l22] = 0.0f;
		}
		for (int l23 = 0; l23 < 2; l23 = faust_wrap_add(l23, 1)) {
			fRec20[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 2; l24 = faust_wrap_add(l24, 1)) {
			fVec3[l24] = 0.0f;
		}
		for (int l25 = 0; l25 < 2; l25 = faust_wrap_add(l25, 1)) {
			fRec21[l25] = 0.0f;
		}
		for (int l26 = 0; l26 < 2; l26 = faust_wrap_add(l26, 1)) {
			iVec4[l26] = 0;
		}
		for (int l27 = 0; l27 < 2; l27 = faust_wrap_add(l27, 1)) {
			iRec22[l27] = 0;
		}
		for (int l28 = 0; l28 < 2; l28 = faust_wrap_add(l28, 1)) {
			iVec5[l28] = 0;
		}
		for (int l29 = 0; l29 < 2; l29 = faust_wrap_add(l29, 1)) {
			iRec24[l29] = 0;
		}
		for (int l30 = 0; l30 < 2; l30 = faust_wrap_add(l30, 1)) {
			fRec23[l30] = 0.0f;
		}
		for (int l31 = 0; l31 < 2; l31 = faust_wrap_add(l31, 1)) {
			iRec28[l31] = 0;
		}
		for (int l32 = 0; l32 < 2; l32 = faust_wrap_add(l32, 1)) {
			fRec25[l32] = 0.0f;
		}
		for (int l33 = 0; l33 < 2; l33 = faust_wrap_add(l33, 1)) {
			iRec26[l33] = 0;
		}
		for (int l34 = 0; l34 < 2; l34 = faust_wrap_add(l34, 1)) {
			fRec27[l34] = 0.0f;
		}
		for (int l35 = 0; l35 < 2; l35 = faust_wrap_add(l35, 1)) {
			iRec29[l35] = 0;
		}
		for (int l36 = 0; l36 < 2; l36 = faust_wrap_add(l36, 1)) {
			fRec30[l36] = 0.0f;
		}
		for (int l37 = 0; l37 < 2; l37 = faust_wrap_add(l37, 1)) {
			fRec31[l37] = 0.0f;
		}
		for (int l38 = 0; l38 < 2; l38 = faust_wrap_add(l38, 1)) {
			fRec32[l38] = 0.0f;
		}
		for (int l39 = 0; l39 < 2; l39 = faust_wrap_add(l39, 1)) {
			iRec33[l39] = 0;
		}
		for (int l40 = 0; l40 < 2; l40 = faust_wrap_add(l40, 1)) {
			iRec34[l40] = 0;
		}
		for (int l41 = 0; l41 < 2; l41 = faust_wrap_add(l41, 1)) {
			fRec35[l41] = 0.0f;
		}
		for (int l42 = 0; l42 < 2; l42 = faust_wrap_add(l42, 1)) {
			fRec36[l42] = 0.0f;
		}
		for (int l43 = 0; l43 < 2; l43 = faust_wrap_add(l43, 1)) {
			fRec37[l43] = 0.0f;
		}
		for (int l44 = 0; l44 < 2; l44 = faust_wrap_add(l44, 1)) {
			iRec38[l44] = 0;
		}
		for (int l45 = 0; l45 < 2; l45 = faust_wrap_add(l45, 1)) {
			fRec39[l45] = 0.0f;
		}
		for (int l46 = 0; l46 < 2; l46 = faust_wrap_add(l46, 1)) {
			iRec40[l46] = 0;
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

	virtual NyxFaustSynth* clone() {
		return new NyxFaustSynth(*this);
	}

	virtual int getSampleRate() {
		return fSampleRate;
	}

	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("NyxSynth");
		ui_interface->addCheckButton("_hasSample", &fCheckbox0);
		ui_interface->addNumEntry("_kind", &fEntry0, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(4.0f), FAUSTFLOAT(1.0f));
		ui_interface->addHorizontalSlider("_speed", &fHslider1, FAUSTFLOAT(1.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(8.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("attack", &fHslider8, FAUSTFLOAT(0.01f), FAUSTFLOAT(0.001f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("biasAttack", &fHslider14, FAUSTFLOAT(0.5f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("biasDecay", &fHslider13, FAUSTFLOAT(0.5f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("biasRelease", &fHslider15, FAUSTFLOAT(0.5f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("decay", &fHslider10, FAUSTFLOAT(0.1f), FAUSTFLOAT(0.001f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("density", &fHslider6, FAUSTFLOAT(15.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->addNumEntry("envelope", &fEntry1, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(6.0f), FAUSTFLOAT(1.0f));
		ui_interface->addHorizontalSlider("final", &fHslider16, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("frequency", &fHslider0, FAUSTFLOAT(2.2e+02f), FAUSTFLOAT(2e+01f), FAUSTFLOAT(8e+03f), FAUSTFLOAT(0.01f));
		ui_interface->addHorizontalSlider("gain", &fHslider7, FAUSTFLOAT(0.2f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addCheckButton("gate", &fCheckbox1);
		ui_interface->addHorizontalSlider("grainMs", &fHslider5, FAUSTFLOAT(8e+01f), FAUSTFLOAT(5.0f), FAUSTFLOAT(5e+02f), FAUSTFLOAT(0.01f));
		ui_interface->addHorizontalSlider("hold", &fHslider17, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("index", &fHslider3, FAUSTFLOAT(2.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(2e+01f), FAUSTFLOAT(0.01f));
		ui_interface->addHorizontalSlider("legato", &fHslider12, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(1.0f));
		ui_interface->addHorizontalSlider("pitch", &fHslider4, FAUSTFLOAT(1.0f), FAUSTFLOAT(0.125f), FAUSTFLOAT(8.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("ratio", &fHslider2, FAUSTFLOAT(2.0f), FAUSTFLOAT(0.01f), FAUSTFLOAT(16.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("release", &fHslider11, FAUSTFLOAT(0.1f), FAUSTFLOAT(0.001f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("sustain", &fHslider9, FAUSTFLOAT(0.8f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->closeBox();
	}

	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* input2 = inputs[2];
		FAUSTFLOAT* input3 = inputs[3];
		FAUSTFLOAT* output0 = outputs[0];
		float fSlow0 = static_cast<float>(fEntry0);
		int iSlow1 = fSlow0 >= 3.0f;
		int iSlow2 = fSlow0 >= 2.0f;
		int iSlow3 = fSlow0 >= 1.0f;
		float fSlow4 = fConst2 * static_cast<float>(fHslider0);
		float fSlow5 = fConst2 * static_cast<float>(fHslider1);
		float fSlow6 = fConst2 * static_cast<float>(fHslider2);
		float fSlow7 = fConst2 * static_cast<float>(fHslider3);
		float fSlow8 = 1.0f - static_cast<float>(fCheckbox0);
		float fSlow9 = fConst2 * static_cast<float>(fHslider4);
		float fSlow10 = fConst2 * static_cast<float>(fHslider5);
		float fSlow11 = fConst2 * static_cast<float>(fHslider6);
		int iSlow12 = fSlow0 >= 4.0f;
		float fSlow13 = fConst2 * static_cast<float>(fHslider7);
		float fSlow14 = static_cast<float>(fEntry1);
		int iSlow15 = fSlow14 >= 4.0f;
		int iSlow16 = fSlow14 >= 2.0f;
		int iSlow17 = fSlow14 >= 1.0f;
		float fSlow18 = static_cast<float>(fHslider8);
		float fSlow19 = fConst0 * fSlow18;
		float fSlow20 = std::max<float>(1.0f, fSlow19);
		float fSlow21 = 1.0f / fSlow20;
		float fSlow22 = static_cast<float>(fCheckbox1);
		float fSlow23 = static_cast<float>(fHslider9);
		float fSlow24 = static_cast<float>(fHslider10);
		float fSlow25 = std::max<float>(1.0f, fConst0 * fSlow24);
		float fSlow26 = (1.0f - fSlow23) / fSlow25;
		float fSlow27 = static_cast<float>(fHslider11);
		float fSlow28 = 1.0f / std::max<float>(1.0f, fConst0 * fSlow27);
		int iSlow29 = fSlow22 == 0.0f;
		int iSlow30 = fSlow22 > 0.0f;
		int iSlow31 = static_cast<int>(fSlow19);
		float fSlow32 = static_cast<float>(iSlow30);
		float fSlow33 = fSlow23 * fSlow32;
		int iSlow34 = fSlow14 >= 3.0f;
		int iSlow35 = iSlow29 > 0;
		float fSlow36 = static_cast<float>(fHslider12);
		int iSlow37 = fSlow36 < 0.5f;
		int iSlow38 = static_cast<int>(fSlow36);
		float fSlow39 = (fSlow23 + -1.0f) / fSlow25;
		float fSlow40 = static_cast<float>(fHslider13);
		float fSlow41 = static_cast<float>(fHslider14);
		float fSlow42 = static_cast<float>(fHslider15);
		float fSlow43 = static_cast<float>(fHslider16);
		float fSlow44 = (1.0f - fSlow43) / fSlow20;
		int iSlow45 = fSlow14 >= 6.0f;
		int iSlow46 = fSlow14 >= 5.0f;
		float fSlow47 = static_cast<float>(fHslider17);
		int iSlow48 = static_cast<int>(fConst0 * (fSlow18 + fSlow47));
		float fSlow49 = fConst0 * fSlow47;
		int iSlow50 = static_cast<int>(fSlow22);
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			iVec1[0] = 1;
			int iTemp0 = faust_wrap_sub(1, iVec1[1]);
			fRec2[0] = fSlow4 + fConst3 * fRec2[1];
			fRec3[0] = fSlow5 + fConst3 * fRec3[1];
			float fTemp1 = fRec2[0] * fRec3[0];
			fRec4[0] = fSlow6 + fConst3 * fRec4[1];
			float fTemp2 = fTemp1 * fRec4[0];
			float fTemp3 = std::min<float>(2e+04f, fTemp2);
			float fTemp4 = ((iTemp0) ? 0.0f : fRec5[1] + fConst1 * fTemp3);
			fRec5[0] = fTemp4 - std::floor(fTemp4);
			fRec6[0] = fSlow7 + fConst3 * fRec6[1];
			float fTemp5 = ((iTemp0) ? 0.0f : fRec1[1] + fConst1 * (std::min<float>(2e+04f, fTemp1) + fTemp3 * ftbl0NyxFaustSynthSIG0[std::max<int>(0, std::min<int>(static_cast<int>(65536.0f * fRec5[0]), 65535))] * fRec6[0]));
			fRec1[0] = fTemp5 - std::floor(fTemp5);
			float fTemp6 = ((iTemp0) ? 0.0f : fRec7[1] + fConst1 * fTemp1);
			fRec7[0] = fTemp6 - std::floor(fTemp6);
			float fTemp7 = static_cast<float>(input0[i0]) + fSlow8 * ftbl0NyxFaustSynthSIG0[std::max<int>(0, std::min<int>(static_cast<int>(65536.0f * fRec7[0]), 65535))];
			fVec2[IOTA0 & 131071] = fTemp7;
			fRec9[0] = fSlow9 + fConst3 * fRec9[1];
			fRec10[0] = fSlow10 + fConst3 * fRec10[1];
			float fTemp8 = std::min<float>(3e+04f, fConst4 * fRec10[0]);
			fRec8[0] = std::fmod(1.0f - std::pow(2.0f, 1.442695f * std::log(std::max<float>(0.001f, fRec9[0]))) + fRec8[1] + fTemp8, fTemp8);
			int iTemp9 = static_cast<int>(fRec8[0]);
			float fTemp10 = std::floor(fRec8[0]);
			float fTemp11 = std::min<float>(fRec8[0] / std::max<float>(1.0f, std::min<float>(1.5e+04f, fConst5 * fRec10[0])), 1.0f);
			float fTemp12 = fTemp8 + fRec8[0];
			int iTemp13 = static_cast<int>(fTemp12);
			float fTemp14 = std::floor(fTemp12);
			fRec12[0] = fSlow11 + fConst3 * fRec12[1];
			float fTemp15 = ((iTemp0) ? 0.0f : fRec11[1] + fConst1 * fRec3[0] * fRec12[0]);
			fRec11[0] = fTemp15 - std::floor(fTemp15);
			float fTemp16 = std::min<float>(1.0f, 0.001f * fRec10[0] * fRec12[0]);
			fRec14[0] = fConst2 * static_cast<float>(input1[i0]) + fConst3 * fRec14[1];
			fRec15[0] = fConst2 * static_cast<float>(input3[i0]) + fConst3 * fRec15[1];
			float fTemp17 = std::pow(2.0f, 2.0f * (fRec14[0] - fRec15[0]));
			float fTemp18 = std::min<float>(2e+04f, fTemp2 * fTemp17);
			float fTemp19 = ((iTemp0) ? 0.0f : fRec16[1] + fConst1 * fTemp18);
			fRec16[0] = fTemp19 - std::floor(fTemp19);
			fRec17[0] = fConst2 * static_cast<float>(input2[i0]) + fConst3 * fRec17[1];
			float fTemp20 = ((iTemp0) ? 0.0f : fRec13[1] + fConst1 * (std::min<float>(2e+04f, fTemp1 * fTemp17) + fRec6[0] * fTemp18 * ftbl0NyxFaustSynthSIG0[std::max<int>(0, std::min<int>(static_cast<int>(65536.0f * fRec16[0]), 65535))] * fRec17[0]));
			fRec13[0] = fTemp20 - std::floor(fTemp20);
			iRec18[0] = faust_wrap_add(12345, faust_wrap_mul(1103515245, iRec18[1]));
			float fTemp21 = 4.656613e-10f * static_cast<float>(iRec18[0]);
			fRec19[0] = 0.5221894f * fRec19[3] + fTemp21 + 2.494956f * fRec19[1] - 2.0172658f * fRec19[2];
			fRec20[0] = fSlow13 + fConst3 * fRec20[1];
			fVec3[0] = fSlow22;
			fRec21[0] = fSlow22 + fRec21[1] * static_cast<float>(fVec3[1] >= fSlow22);
			iVec4[0] = iSlow29;
			iRec22[0] = faust_wrap_mul(iSlow29, faust_wrap_add(1, iRec22[1]));
			iVec5[0] = iSlow30;
			iRec24[0] = faust_wrap_mul(iSlow30, faust_wrap_add(1, iRec24[1]));
			int iTemp22 = faust_wrap_sub(iSlow30, iVec5[1]);
			int iTemp23 = faust_wrap_mul(iTemp22, iTemp22 > 0);
			int iTemp24 = (iRec24[0] < iSlow31) | iTemp23;
			float fTemp25 = 0.1447178f * ((iSlow30) ? ((iTemp24) ? fSlow18 : fSlow24) : fSlow27);
			int iTemp26 = std::fabs(fTemp25) < 1.1920929e-07f;
			float fTemp27 = ((iTemp26) ? 0.0f : std::exp(-(fConst1 / ((iTemp26) ? 1.0f : fTemp25))));
			fRec23[0] = (1.0f - fTemp27) * ((iSlow30) ? ((iTemp24) ? fSlow32 : fSlow33) : 0.0f) + fTemp27 * fRec23[1];
			int iTemp28 = (iVec4[1] <= 0) & iSlow35;
			float fTemp29 = static_cast<float>(faust_wrap_sub(1, iTemp28));
			iRec28[0] = faust_wrap_add(1, iRec28[1]);
			int iTemp30 = iRec28[1] == 0;
			int iTemp31 = (iTemp23 & iSlow37) | iTemp30;
			float fTemp32 = ((iTemp31) ? 0.0f : fRec27[1]);
			float fTemp33 = static_cast<float>(iTemp28);
			fRec25[0] = fRec25[1] * fTemp29 + ((iTemp30) ? 1.0f : fTemp32) * fTemp33;
			int iTemp34 = ((iSlow38) ? iRec26[1] : ((iTemp23) ? 0 : iRec26[1]));
			int iTemp35 = iTemp34 == 0;
			int iTemp36 = iTemp34 == 1;
			iRec26[0] = ((iSlow30) ? ((iTemp35) ? 1 : ((iTemp36) ? ((fTemp32 < 1.0f) ? 1 : 2) : 2)) : 0);
			float fTemp37 = ((iTemp34 == 2) ? fSlow23 : 0.0f);
			float fTemp38 = std::max<float>(0.03f, std::min<float>(0.97f, ((iTemp35) ? fSlow42 : ((iTemp36) ? fSlow41 : fSlow40))));
			float fTemp39 = -1.0f + fTemp38;
			float fTemp40 = ((iTemp35) ? fRec25[0] : 1.0f);
			float fTemp41 = ((fTemp37 == fTemp40) ? 0.5f : (fTemp32 - fTemp37) / (fTemp40 - fTemp37));
			float fTemp42 = fTemp39 * fTemp41 / (fTemp38 * (-1.0f + 2.0f * fTemp41) - fTemp41);
			fRec27[0] = std::max<float>(fTemp37, std::min<float>(1.0f, fTemp32 - ((iTemp35) ? -(fSlow28 * fRec25[0]) : ((iTemp36) ? fSlow21 : fSlow39)) * fTemp38 * fTemp39 / NyxFaustSynth_faustpower2_f(1.0f - fTemp42 + fTemp38 * (-1.0f + 2.0f * fTemp42))));
			int iTemp43 = ((iSlow38) ? iRec29[1] : ((iTemp23) ? 0 : iRec29[1]));
			int iTemp44 = iTemp43 == 0;
			int iTemp45 = iTemp43 == 1;
			float fTemp46 = ((iTemp31) ? fSlow43 : fRec31[1]);
			iRec29[0] = ((iSlow30) ? ((iTemp44) ? 1 : ((iTemp45) ? ((fTemp46 < 1.0f) ? 1 : 2) : 2)) : 0);
			fRec30[0] = fTemp29 * fRec30[1] + fTemp33 * ((iTemp30) ? 1.0f : fTemp46);
			float fTemp47 = ((iTemp43 == 2) ? fSlow23 : fSlow43);
			float fTemp48 = std::max<float>(0.03f, std::min<float>(0.97f, ((iTemp44) ? fSlow42 : ((iTemp45) ? fSlow41 : fSlow40))));
			float fTemp49 = -1.0f + fTemp48;
			float fTemp50 = ((iTemp44) ? fRec30[0] : 1.0f);
			float fTemp51 = ((fTemp47 == fTemp50) ? 0.5f : (fTemp46 - fTemp47) / (fTemp50 - fTemp47));
			float fTemp52 = fTemp49 * fTemp51 / (fTemp48 * (-1.0f + 2.0f * fTemp51) - fTemp51);
			fRec31[0] = std::max<float>(fTemp47, std::min<float>(1.0f, fTemp46 - ((iTemp44) ? fSlow28 * (fSlow43 - fRec30[0]) : ((iTemp45) ? fSlow44 : fSlow39)) * fTemp48 * fTemp49 / NyxFaustSynth_faustpower2_f(1.0f - fTemp52 + fTemp48 * (-1.0f + 2.0f * fTemp52))));
			int iTemp53 = (iRec24[0] < iSlow48) & iSlow30;
			float fTemp54 = 0.1447178f * ((iSlow30) ? ((iTemp53) ? fSlow18 : fSlow24) : fSlow27);
			int iTemp55 = std::fabs(fTemp54) < 1.1920929e-07f;
			float fTemp56 = ((iTemp55) ? 0.0f : std::exp(-(fConst1 / ((iTemp55) ? 1.0f : fTemp54))));
			fRec32[0] = (1.0f - fTemp56) * ((iSlow30) ? ((iTemp53) ? fSlow32 : fSlow33) : 0.0f) + fTemp56 * fRec32[1];
			int iTemp57 = ((iSlow38) ? iRec34[1] : ((iTemp23) ? 0 : iRec34[1]));
			iRec33[0] = faust_wrap_mul(iSlow30, faust_wrap_add(1, faust_wrap_mul(iRec33[1], iTemp57 != 1)));
			int iTemp58 = iTemp57 >= 2;
			int iTemp59 = iTemp57 >= 1;
			float fTemp60 = ((iTemp31) ? 0.0f : fRec36[1]);
			int iTemp61 = iTemp57 >= 3;
			iRec34[0] = ((iSlow30) ? ((iTemp58) ? ((iTemp61) ? 3 : ((static_cast<float>(iRec33[0]) < fSlow49) ? 2 : 3)) : ((iTemp59) ? ((fTemp60 < 1.0f) ? 1 : 2) : 1)) : 0);
			fRec35[0] = fTemp29 * fRec35[1] + fTemp33 * ((iTemp30) ? 1.0f : fTemp60);
			float fTemp62 = ((iTemp57 == 3) ? fSlow23 : 0.0f);
			float fTemp63 = fSlow28 * fRec35[0];
			float fTemp64 = std::max<float>(0.03f, std::min<float>(0.97f, ((iTemp58) ? ((iTemp61) ? fSlow40 : 0.5f) : ((iTemp59) ? fSlow41 : fSlow42))));
			float fTemp65 = -1.0f + fTemp64;
			float fTemp66 = ((iTemp57 == 0) ? fRec35[0] : 1.0f);
			float fTemp67 = ((fTemp62 == fTemp66) ? 0.5f : (fTemp60 - fTemp62) / (fTemp66 - fTemp62));
			float fTemp68 = fTemp65 * fTemp67 / (fTemp64 * (-1.0f + 2.0f * fTemp67) - fTemp67);
			fRec36[0] = std::max<float>(fTemp62, std::min<float>(1.0f, fTemp60 - ((iTemp58) ? ((iTemp61) ? fSlow39 : ((iSlow50) ? fSlow21 : -fTemp63)) : ((iTemp59) ? fSlow21 : -fTemp63)) * fTemp64 * fTemp65 / NyxFaustSynth_faustpower2_f(1.0f - fTemp68 + fTemp64 * (-1.0f + 2.0f * fTemp68))));
			float fTemp69 = ((iTemp31) ? fSlow43 : fRec39[1]);
			fRec37[0] = fTemp29 * fRec37[1] + fTemp33 * ((iTemp30) ? 1.0f : fTemp69);
			int iTemp70 = ((iSlow38) ? iRec40[1] : ((iTemp23) ? 0 : iRec40[1]));
			iRec38[0] = faust_wrap_mul(iSlow30, faust_wrap_add(1, faust_wrap_mul(iRec38[1], iTemp70 != 1)));
			float fTemp71 = ((iTemp70 == 3) ? fSlow23 : fSlow43);
			int iTemp72 = iTemp70 >= 2;
			int iTemp73 = iTemp70 >= 1;
			float fTemp74 = fSlow28 * (fSlow43 - fRec37[0]);
			int iTemp75 = iTemp70 >= 3;
			float fTemp76 = std::max<float>(0.03f, std::min<float>(0.97f, ((iTemp72) ? ((iTemp75) ? fSlow40 : 0.5f) : ((iTemp73) ? fSlow41 : fSlow42))));
			float fTemp77 = -1.0f + fTemp76;
			float fTemp78 = ((iTemp70 == 0) ? fRec37[0] : 1.0f);
			float fTemp79 = ((fTemp71 == fTemp78) ? 0.5f : (fTemp69 - fTemp71) / (fTemp78 - fTemp71));
			float fTemp80 = fTemp77 * fTemp79 / (fTemp76 * (-1.0f + 2.0f * fTemp79) - fTemp79);
			fRec39[0] = std::max<float>(fTemp71, std::min<float>(1.0f, fTemp69 - ((iTemp72) ? ((iTemp75) ? fSlow39 : ((iSlow50) ? fSlow44 : fTemp74)) : ((iTemp73) ? fSlow44 : fTemp74)) * fTemp76 * fTemp77 / NyxFaustSynth_faustpower2_f(1.0f - fTemp80 + fTemp76 * (-1.0f + 2.0f * fTemp80))));
			iRec40[0] = ((iSlow30) ? ((iTemp72) ? ((iTemp75) ? 3 : ((static_cast<float>(iRec38[0]) < fSlow49) ? 2 : 3)) : ((iTemp73) ? ((fTemp69 < 1.0f) ? 1 : 2) : 1)) : 0);
			output0[i0] = static_cast<FAUSTFLOAT>(((iSlow1) ? ((iSlow12) ? 0.049922034f * fRec19[0] + 0.0506127f * fRec19[2] - (0.095993534f * fRec19[1] + 0.004408786f * fRec19[3]) : fTemp21) : ((iSlow2) ? ftbl0NyxFaustSynthSIG0[std::max<int>(0, std::min<int>(static_cast<int>(65536.0f * fRec13[0]), 65535))] * (0.7152f * fRec17[0] + 0.2126f * fRec14[0] + 0.0722f * fRec15[0]) : ((iSlow3) ? 0.5f * ((fVec2[(faust_wrap_sub(IOTA0, std::min<int>(65537, std::max<int>(0, iTemp9)))) & 131071] * (1.0f - fRec8[0] + fTemp10) + (fRec8[0] - fTemp10) * fVec2[(faust_wrap_sub(IOTA0, std::min<int>(65537, std::max<int>(0, faust_wrap_add(1, iTemp9))))) & 131071]) * fTemp11 + (fVec2[(faust_wrap_sub(IOTA0, std::min<int>(65537, std::max<int>(0, iTemp13)))) & 131071] * (1.0f - fTemp12 + fTemp14) + (fTemp12 - fTemp14) * fVec2[(faust_wrap_sub(IOTA0, std::min<int>(65537, std::max<int>(0, faust_wrap_add(1, iTemp13))))) & 131071]) * (1.0f - fTemp11)) * static_cast<float>(fRec11[0] < fTemp16) * (1.0f - std::cos(6.2831855f * std::min<float>(1.0f, fRec11[0] / std::max<float>(0.001f, fTemp16)))) : ftbl0NyxFaustSynthSIG0[std::max<int>(0, std::min<int>(static_cast<int>(65536.0f * fRec1[0]), 65535))]))) * fRec20[0] * ((iSlow15) ? ((iSlow45) ? fRec39[0] : ((iSlow46) ? fRec36[0] : fRec32[0])) : ((iSlow16) ? ((iSlow34) ? fRec31[0] : fRec27[0]) : ((iSlow17) ? fRec23[0] : std::max<float>(0.0f, std::min<float>(fSlow21 * fRec21[0], std::max<float>(1.0f + fSlow26 * (fSlow20 - fRec21[0]), fSlow23)) * (1.0f - fSlow28 * static_cast<float>(iRec22[0])))))));
			iVec1[1] = iVec1[0];
			fRec2[1] = fRec2[0];
			fRec3[1] = fRec3[0];
			fRec4[1] = fRec4[0];
			fRec5[1] = fRec5[0];
			fRec6[1] = fRec6[0];
			fRec1[1] = fRec1[0];
			fRec7[1] = fRec7[0];
			IOTA0 = faust_wrap_add(IOTA0, 1);
			fRec9[1] = fRec9[0];
			fRec10[1] = fRec10[0];
			fRec8[1] = fRec8[0];
			fRec12[1] = fRec12[0];
			fRec11[1] = fRec11[0];
			fRec14[1] = fRec14[0];
			fRec15[1] = fRec15[0];
			fRec16[1] = fRec16[0];
			fRec17[1] = fRec17[0];
			fRec13[1] = fRec13[0];
			iRec18[1] = iRec18[0];
			for (int j0 = 3; j0 > 0; j0 = faust_wrap_sub(j0, 1)) {
				fRec19[j0] = fRec19[faust_wrap_sub(j0, 1)];
			}
			fRec20[1] = fRec20[0];
			fVec3[1] = fVec3[0];
			fRec21[1] = fRec21[0];
			iVec4[1] = iVec4[0];
			iRec22[1] = iRec22[0];
			iVec5[1] = iVec5[0];
			iRec24[1] = iRec24[0];
			fRec23[1] = fRec23[0];
			iRec28[1] = iRec28[0];
			fRec25[1] = fRec25[0];
			iRec26[1] = iRec26[0];
			fRec27[1] = fRec27[0];
			iRec29[1] = iRec29[0];
			fRec30[1] = fRec30[0];
			fRec31[1] = fRec31[0];
			fRec32[1] = fRec32[0];
			iRec33[1] = iRec33[0];
			iRec34[1] = iRec34[0];
			fRec35[1] = fRec35[0];
			fRec36[1] = fRec36[0];
			fRec37[1] = fRec37[0];
			iRec38[1] = iRec38[0];
			fRec39[1] = fRec39[0];
			iRec40[1] = iRec40[0];
		}
	}

};

#endif
