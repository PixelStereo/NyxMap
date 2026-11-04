/* ------------------------------------------------------------
name: "Reverb"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFxReverb -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __NyxFxReverb_H__
#define  __NyxFxReverb_H__

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
#define FAUSTCLASS NyxFxReverb
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


class NyxFxReverb : public dsp {

 private:

	int fSampleRate;
	float fConst0;
	float fConst1;
	FAUSTFLOAT fHslider0;
	float fConst2;
	float fRec6[2];
	float fRec4[2];
	FAUSTFLOAT fHslider1;
	float fRec7[2];
	int IOTA0;
	float fVec0[8192];
	int iConst3;
	int iConst4;
	float fRec5[2];
	float fRec8[2];
	float fVec1[8192];
	int iConst5;
	int iConst6;
	float fRec9[2];
	float fRec10[2];
	float fVec2[8192];
	int iConst7;
	int iConst8;
	float fRec11[2];
	float fRec12[2];
	float fVec3[8192];
	int iConst9;
	int iConst10;
	float fRec13[2];
	float fRec14[2];
	float fVec4[8192];
	int iConst11;
	int iConst12;
	float fRec15[2];
	float fRec16[2];
	float fVec5[8192];
	int iConst13;
	int iConst14;
	float fRec17[2];
	float fRec18[2];
	float fVec6[8192];
	int iConst15;
	int iConst16;
	float fRec19[2];
	float fRec20[2];
	float fVec7[8192];
	int iConst17;
	int iConst18;
	float fRec21[2];
	float fVec8[2048];
	int iConst19;
	int iConst20;
	float fRec3[2];
	float fVec9[2048];
	int iConst21;
	int iConst22;
	float fRec2[2];
	float fVec10[2048];
	int iConst23;
	int iConst24;
	float fRec1[2];
	float fVec11[1024];
	int iConst25;
	int iConst26;
	float fRec0[2];
	float fRec26[2];
	float fVec12[8192];
	int iConst27;
	float fRec27[2];
	float fRec28[2];
	float fVec13[8192];
	int iConst28;
	float fRec29[2];
	float fRec30[2];
	float fVec14[8192];
	int iConst29;
	float fRec31[2];
	float fRec32[2];
	float fVec15[8192];
	int iConst30;
	float fRec33[2];
	float fRec34[2];
	float fVec16[8192];
	int iConst31;
	float fRec35[2];
	float fRec36[2];
	float fVec17[8192];
	int iConst32;
	float fRec37[2];
	float fRec38[2];
	float fVec18[8192];
	int iConst33;
	float fRec39[2];
	float fRec40[2];
	float fVec19[8192];
	int iConst34;
	float fRec41[2];
	float fVec20[2048];
	int iConst35;
	float fRec25[2];
	float fVec21[2048];
	int iConst36;
	float fRec24[2];
	float fVec22[2048];
	int iConst37;
	float fRec23[2];
	float fVec23[1024];
	int iConst38;
	float fRec22[2];

 public:
	NyxFxReverb() {
	}

	NyxFxReverb(const NyxFxReverb&) = default;

	virtual ~NyxFxReverb() = default;

	NyxFxReverb& operator=(const NyxFxReverb&) = default;

	void metadata(Meta* m) {
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFxReverb -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("delays.lib/name", "Faust Delay Library");
		m->declare("delays.lib/version", "1.2.0");
		m->declare("filename", "Reverb.dsp");
		m->declare("filters.lib/allpass_comb:author", "Julius O. Smith III");
		m->declare("filters.lib/allpass_comb:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/allpass_comb:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/version", "1.9.0");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LicenseRef-LGPL-2.1-or-later-with-Faust-exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "Reverb");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("reverbs.lib/mono_freeverb:author", "Romain Michon");
		m->declare("reverbs.lib/name", "Faust Reverb Library");
		m->declare("reverbs.lib/stereo_freeverb:author", "Romain Michon");
		m->declare("reverbs.lib/version", "1.5.1");
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
		iConst3 = static_cast<int>(0.036666665f * fConst0);
		iConst4 = std::max<int>(0, faust_wrap_add(iConst3, -1));
		iConst5 = static_cast<int>(0.035306122f * fConst0);
		iConst6 = std::max<int>(0, faust_wrap_add(iConst5, -1));
		iConst7 = static_cast<int>(0.033809524f * fConst0);
		iConst8 = std::max<int>(0, faust_wrap_add(iConst7, -1));
		iConst9 = static_cast<int>(0.0322449f * fConst0);
		iConst10 = std::max<int>(0, faust_wrap_add(iConst9, -1));
		iConst11 = static_cast<int>(0.030748298f * fConst0);
		iConst12 = std::max<int>(0, faust_wrap_add(iConst11, -1));
		iConst13 = static_cast<int>(0.028956916f * fConst0);
		iConst14 = std::max<int>(0, faust_wrap_add(iConst13, -1));
		iConst15 = static_cast<int>(0.026938776f * fConst0);
		iConst16 = std::max<int>(0, faust_wrap_add(iConst15, -1));
		iConst17 = static_cast<int>(0.025306122f * fConst0);
		iConst18 = std::max<int>(0, faust_wrap_add(iConst17, -1));
		iConst19 = static_cast<int>(0.0126077095f * fConst0);
		iConst20 = std::min<int>(1024, std::max<int>(0, faust_wrap_add(iConst19, -1)));
		iConst21 = static_cast<int>(0.01f * fConst0);
		iConst22 = std::min<int>(1024, std::max<int>(0, faust_wrap_add(iConst21, -1)));
		iConst23 = static_cast<int>(0.0077324263f * fConst0);
		iConst24 = std::min<int>(1024, std::max<int>(0, faust_wrap_add(iConst23, -1)));
		iConst25 = static_cast<int>(0.0051020407f * fConst0);
		iConst26 = std::min<int>(1024, std::max<int>(0, faust_wrap_add(iConst25, -1)));
		iConst27 = std::max<int>(0, faust_wrap_add(iConst3, 22));
		iConst28 = std::max<int>(0, faust_wrap_add(iConst5, 22));
		iConst29 = std::max<int>(0, faust_wrap_add(iConst7, 22));
		iConst30 = std::max<int>(0, faust_wrap_add(iConst9, 22));
		iConst31 = std::max<int>(0, faust_wrap_add(iConst11, 22));
		iConst32 = std::max<int>(0, faust_wrap_add(iConst13, 22));
		iConst33 = std::max<int>(0, faust_wrap_add(iConst15, 22));
		iConst34 = std::max<int>(0, faust_wrap_add(iConst17, 22));
		iConst35 = std::min<int>(1024, std::max<int>(0, faust_wrap_add(iConst19, 22)));
		iConst36 = std::min<int>(1024, std::max<int>(0, faust_wrap_add(iConst21, 22)));
		iConst37 = std::min<int>(1024, std::max<int>(0, faust_wrap_add(iConst23, 22)));
		iConst38 = std::min<int>(1024, std::max<int>(0, faust_wrap_add(iConst25, 22)));
	}

	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(0.5f);
		fHslider1 = static_cast<FAUSTFLOAT>(0.6f);
	}

	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = faust_wrap_add(l0, 1)) {
			fRec6[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 2; l1 = faust_wrap_add(l1, 1)) {
			fRec4[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 2; l2 = faust_wrap_add(l2, 1)) {
			fRec7[l2] = 0.0f;
		}
		IOTA0 = 0;
		for (int l3 = 0; l3 < 8192; l3 = faust_wrap_add(l3, 1)) {
			fVec0[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 2; l4 = faust_wrap_add(l4, 1)) {
			fRec5[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = faust_wrap_add(l5, 1)) {
			fRec8[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 8192; l6 = faust_wrap_add(l6, 1)) {
			fVec1[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 2; l7 = faust_wrap_add(l7, 1)) {
			fRec9[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 2; l8 = faust_wrap_add(l8, 1)) {
			fRec10[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 8192; l9 = faust_wrap_add(l9, 1)) {
			fVec2[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 2; l10 = faust_wrap_add(l10, 1)) {
			fRec11[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 2; l11 = faust_wrap_add(l11, 1)) {
			fRec12[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 8192; l12 = faust_wrap_add(l12, 1)) {
			fVec3[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 2; l13 = faust_wrap_add(l13, 1)) {
			fRec13[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 2; l14 = faust_wrap_add(l14, 1)) {
			fRec14[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 8192; l15 = faust_wrap_add(l15, 1)) {
			fVec4[l15] = 0.0f;
		}
		for (int l16 = 0; l16 < 2; l16 = faust_wrap_add(l16, 1)) {
			fRec15[l16] = 0.0f;
		}
		for (int l17 = 0; l17 < 2; l17 = faust_wrap_add(l17, 1)) {
			fRec16[l17] = 0.0f;
		}
		for (int l18 = 0; l18 < 8192; l18 = faust_wrap_add(l18, 1)) {
			fVec5[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 2; l19 = faust_wrap_add(l19, 1)) {
			fRec17[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 2; l20 = faust_wrap_add(l20, 1)) {
			fRec18[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 8192; l21 = faust_wrap_add(l21, 1)) {
			fVec6[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 2; l22 = faust_wrap_add(l22, 1)) {
			fRec19[l22] = 0.0f;
		}
		for (int l23 = 0; l23 < 2; l23 = faust_wrap_add(l23, 1)) {
			fRec20[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 8192; l24 = faust_wrap_add(l24, 1)) {
			fVec7[l24] = 0.0f;
		}
		for (int l25 = 0; l25 < 2; l25 = faust_wrap_add(l25, 1)) {
			fRec21[l25] = 0.0f;
		}
		for (int l26 = 0; l26 < 2048; l26 = faust_wrap_add(l26, 1)) {
			fVec8[l26] = 0.0f;
		}
		for (int l27 = 0; l27 < 2; l27 = faust_wrap_add(l27, 1)) {
			fRec3[l27] = 0.0f;
		}
		for (int l28 = 0; l28 < 2048; l28 = faust_wrap_add(l28, 1)) {
			fVec9[l28] = 0.0f;
		}
		for (int l29 = 0; l29 < 2; l29 = faust_wrap_add(l29, 1)) {
			fRec2[l29] = 0.0f;
		}
		for (int l30 = 0; l30 < 2048; l30 = faust_wrap_add(l30, 1)) {
			fVec10[l30] = 0.0f;
		}
		for (int l31 = 0; l31 < 2; l31 = faust_wrap_add(l31, 1)) {
			fRec1[l31] = 0.0f;
		}
		for (int l32 = 0; l32 < 1024; l32 = faust_wrap_add(l32, 1)) {
			fVec11[l32] = 0.0f;
		}
		for (int l33 = 0; l33 < 2; l33 = faust_wrap_add(l33, 1)) {
			fRec0[l33] = 0.0f;
		}
		for (int l34 = 0; l34 < 2; l34 = faust_wrap_add(l34, 1)) {
			fRec26[l34] = 0.0f;
		}
		for (int l35 = 0; l35 < 8192; l35 = faust_wrap_add(l35, 1)) {
			fVec12[l35] = 0.0f;
		}
		for (int l36 = 0; l36 < 2; l36 = faust_wrap_add(l36, 1)) {
			fRec27[l36] = 0.0f;
		}
		for (int l37 = 0; l37 < 2; l37 = faust_wrap_add(l37, 1)) {
			fRec28[l37] = 0.0f;
		}
		for (int l38 = 0; l38 < 8192; l38 = faust_wrap_add(l38, 1)) {
			fVec13[l38] = 0.0f;
		}
		for (int l39 = 0; l39 < 2; l39 = faust_wrap_add(l39, 1)) {
			fRec29[l39] = 0.0f;
		}
		for (int l40 = 0; l40 < 2; l40 = faust_wrap_add(l40, 1)) {
			fRec30[l40] = 0.0f;
		}
		for (int l41 = 0; l41 < 8192; l41 = faust_wrap_add(l41, 1)) {
			fVec14[l41] = 0.0f;
		}
		for (int l42 = 0; l42 < 2; l42 = faust_wrap_add(l42, 1)) {
			fRec31[l42] = 0.0f;
		}
		for (int l43 = 0; l43 < 2; l43 = faust_wrap_add(l43, 1)) {
			fRec32[l43] = 0.0f;
		}
		for (int l44 = 0; l44 < 8192; l44 = faust_wrap_add(l44, 1)) {
			fVec15[l44] = 0.0f;
		}
		for (int l45 = 0; l45 < 2; l45 = faust_wrap_add(l45, 1)) {
			fRec33[l45] = 0.0f;
		}
		for (int l46 = 0; l46 < 2; l46 = faust_wrap_add(l46, 1)) {
			fRec34[l46] = 0.0f;
		}
		for (int l47 = 0; l47 < 8192; l47 = faust_wrap_add(l47, 1)) {
			fVec16[l47] = 0.0f;
		}
		for (int l48 = 0; l48 < 2; l48 = faust_wrap_add(l48, 1)) {
			fRec35[l48] = 0.0f;
		}
		for (int l49 = 0; l49 < 2; l49 = faust_wrap_add(l49, 1)) {
			fRec36[l49] = 0.0f;
		}
		for (int l50 = 0; l50 < 8192; l50 = faust_wrap_add(l50, 1)) {
			fVec17[l50] = 0.0f;
		}
		for (int l51 = 0; l51 < 2; l51 = faust_wrap_add(l51, 1)) {
			fRec37[l51] = 0.0f;
		}
		for (int l52 = 0; l52 < 2; l52 = faust_wrap_add(l52, 1)) {
			fRec38[l52] = 0.0f;
		}
		for (int l53 = 0; l53 < 8192; l53 = faust_wrap_add(l53, 1)) {
			fVec18[l53] = 0.0f;
		}
		for (int l54 = 0; l54 < 2; l54 = faust_wrap_add(l54, 1)) {
			fRec39[l54] = 0.0f;
		}
		for (int l55 = 0; l55 < 2; l55 = faust_wrap_add(l55, 1)) {
			fRec40[l55] = 0.0f;
		}
		for (int l56 = 0; l56 < 8192; l56 = faust_wrap_add(l56, 1)) {
			fVec19[l56] = 0.0f;
		}
		for (int l57 = 0; l57 < 2; l57 = faust_wrap_add(l57, 1)) {
			fRec41[l57] = 0.0f;
		}
		for (int l58 = 0; l58 < 2048; l58 = faust_wrap_add(l58, 1)) {
			fVec20[l58] = 0.0f;
		}
		for (int l59 = 0; l59 < 2; l59 = faust_wrap_add(l59, 1)) {
			fRec25[l59] = 0.0f;
		}
		for (int l60 = 0; l60 < 2048; l60 = faust_wrap_add(l60, 1)) {
			fVec21[l60] = 0.0f;
		}
		for (int l61 = 0; l61 < 2; l61 = faust_wrap_add(l61, 1)) {
			fRec24[l61] = 0.0f;
		}
		for (int l62 = 0; l62 < 2048; l62 = faust_wrap_add(l62, 1)) {
			fVec22[l62] = 0.0f;
		}
		for (int l63 = 0; l63 < 2; l63 = faust_wrap_add(l63, 1)) {
			fRec23[l63] = 0.0f;
		}
		for (int l64 = 0; l64 < 1024; l64 = faust_wrap_add(l64, 1)) {
			fVec23[l64] = 0.0f;
		}
		for (int l65 = 0; l65 < 2; l65 = faust_wrap_add(l65, 1)) {
			fRec22[l65] = 0.0f;
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

	virtual NyxFxReverb* clone() {
		return new NyxFxReverb(*this);
	}

	virtual int getSampleRate() {
		return fSampleRate;
	}

	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("Reverb");
		ui_interface->addHorizontalSlider("damping", &fHslider0, FAUSTFLOAT(0.5f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("room", &fHslider1, FAUSTFLOAT(0.6f), FAUSTFLOAT(0.0f), FAUSTFLOAT(0.98f), FAUSTFLOAT(0.001f));
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
			fRec6[0] = fSlow0 + fConst2 * fRec6[1];
			float fTemp0 = 1.0f - fRec6[0];
			fRec4[0] = fRec6[0] * fRec4[1] + fTemp0 * fRec5[1];
			float fTemp1 = static_cast<float>(input0[i0]) + static_cast<float>(input1[i0]);
			fRec7[0] = fSlow1 + fConst2 * fRec7[1];
			float fTemp2 = 0.7f + 0.28f * fRec7[0];
			fVec0[IOTA0 & 8191] = fTemp1 + fTemp2 * fRec4[0];
			fRec5[0] = fVec0[(faust_wrap_sub(IOTA0, iConst4)) & 8191];
			fRec8[0] = fRec6[0] * fRec8[1] + fTemp0 * fRec9[1];
			fVec1[IOTA0 & 8191] = fTemp1 + fTemp2 * fRec8[0];
			fRec9[0] = fVec1[(faust_wrap_sub(IOTA0, iConst6)) & 8191];
			fRec10[0] = fRec6[0] * fRec10[1] + fTemp0 * fRec11[1];
			fVec2[IOTA0 & 8191] = fTemp1 + fTemp2 * fRec10[0];
			fRec11[0] = fVec2[(faust_wrap_sub(IOTA0, iConst8)) & 8191];
			fRec12[0] = fRec6[0] * fRec12[1] + fTemp0 * fRec13[1];
			fVec3[IOTA0 & 8191] = fTemp1 + fTemp2 * fRec12[0];
			fRec13[0] = fVec3[(faust_wrap_sub(IOTA0, iConst10)) & 8191];
			fRec14[0] = fRec6[0] * fRec14[1] + fTemp0 * fRec15[1];
			fVec4[IOTA0 & 8191] = fTemp1 + fTemp2 * fRec14[0];
			fRec15[0] = fVec4[(faust_wrap_sub(IOTA0, iConst12)) & 8191];
			fRec16[0] = fRec6[0] * fRec16[1] + fTemp0 * fRec17[1];
			fVec5[IOTA0 & 8191] = fTemp1 + fTemp2 * fRec16[0];
			fRec17[0] = fVec5[(faust_wrap_sub(IOTA0, iConst14)) & 8191];
			fRec18[0] = fRec6[0] * fRec18[1] + fTemp0 * fRec19[1];
			fVec6[IOTA0 & 8191] = fTemp1 + fTemp2 * fRec18[0];
			fRec19[0] = fVec6[(faust_wrap_sub(IOTA0, iConst16)) & 8191];
			fRec20[0] = fRec20[1] * fRec6[0] + fRec21[1] * fTemp0;
			fVec7[IOTA0 & 8191] = fTemp1 + fRec20[0] * fTemp2;
			fRec21[0] = fVec7[(faust_wrap_sub(IOTA0, iConst18)) & 8191];
			float fTemp3 = fRec5[1] + fRec9[1] + fRec11[1] + fRec13[1] + fRec15[1] + fRec17[1] + fRec19[1] + 0.5f * fRec3[1] + fRec21[1];
			fVec8[IOTA0 & 2047] = fTemp3;
			fRec3[0] = fVec8[(faust_wrap_sub(IOTA0, iConst20)) & 2047];
			float fTemp4 = fRec3[1] + 0.5f * (fRec2[1] - fTemp3);
			fVec9[IOTA0 & 2047] = fTemp4;
			fRec2[0] = fVec9[(faust_wrap_sub(IOTA0, iConst22)) & 2047];
			float fTemp5 = fRec2[1] + 0.5f * (fRec1[1] - fTemp4);
			fVec10[IOTA0 & 2047] = fTemp5;
			fRec1[0] = fVec10[(faust_wrap_sub(IOTA0, iConst24)) & 2047];
			float fTemp6 = fRec1[1] + 0.5f * (fRec0[1] - fTemp5);
			fVec11[IOTA0 & 1023] = fTemp6;
			fRec0[0] = fVec11[(faust_wrap_sub(IOTA0, iConst26)) & 1023];
			output0[i0] = static_cast<FAUSTFLOAT>(0.15f * (fRec0[1] - 0.5f * fTemp6));
			fRec26[0] = fRec6[0] * fRec26[1] + fTemp0 * fRec27[1];
			fVec12[IOTA0 & 8191] = fTemp1 + fTemp2 * fRec26[0];
			fRec27[0] = fVec12[(faust_wrap_sub(IOTA0, iConst27)) & 8191];
			fRec28[0] = fRec6[0] * fRec28[1] + fTemp0 * fRec29[1];
			fVec13[IOTA0 & 8191] = fTemp1 + fTemp2 * fRec28[0];
			fRec29[0] = fVec13[(faust_wrap_sub(IOTA0, iConst28)) & 8191];
			fRec30[0] = fRec6[0] * fRec30[1] + fTemp0 * fRec31[1];
			fVec14[IOTA0 & 8191] = fTemp1 + fTemp2 * fRec30[0];
			fRec31[0] = fVec14[(faust_wrap_sub(IOTA0, iConst29)) & 8191];
			fRec32[0] = fRec6[0] * fRec32[1] + fTemp0 * fRec33[1];
			fVec15[IOTA0 & 8191] = fTemp1 + fTemp2 * fRec32[0];
			fRec33[0] = fVec15[(faust_wrap_sub(IOTA0, iConst30)) & 8191];
			fRec34[0] = fRec6[0] * fRec34[1] + fTemp0 * fRec35[1];
			fVec16[IOTA0 & 8191] = fTemp1 + fTemp2 * fRec34[0];
			fRec35[0] = fVec16[(faust_wrap_sub(IOTA0, iConst31)) & 8191];
			fRec36[0] = fRec6[0] * fRec36[1] + fTemp0 * fRec37[1];
			fVec17[IOTA0 & 8191] = fTemp1 + fTemp2 * fRec36[0];
			fRec37[0] = fVec17[(faust_wrap_sub(IOTA0, iConst32)) & 8191];
			fRec38[0] = fRec6[0] * fRec38[1] + fTemp0 * fRec39[1];
			fVec18[IOTA0 & 8191] = fTemp1 + fTemp2 * fRec38[0];
			fRec39[0] = fVec18[(faust_wrap_sub(IOTA0, iConst33)) & 8191];
			fRec40[0] = fRec6[0] * fRec40[1] + fTemp0 * fRec41[1];
			fVec19[IOTA0 & 8191] = fTemp1 + fTemp2 * fRec40[0];
			fRec41[0] = fVec19[(faust_wrap_sub(IOTA0, iConst34)) & 8191];
			float fTemp7 = fRec27[1] + fRec29[1] + fRec31[1] + fRec33[1] + fRec35[1] + fRec37[1] + fRec39[1] + 0.5f * fRec25[1] + fRec41[1];
			fVec20[IOTA0 & 2047] = fTemp7;
			fRec25[0] = fVec20[(faust_wrap_sub(IOTA0, iConst35)) & 2047];
			float fTemp8 = fRec25[1] + 0.5f * (fRec24[1] - fTemp7);
			fVec21[IOTA0 & 2047] = fTemp8;
			fRec24[0] = fVec21[(faust_wrap_sub(IOTA0, iConst36)) & 2047];
			float fTemp9 = fRec24[1] + 0.5f * (fRec23[1] - fTemp8);
			fVec22[IOTA0 & 2047] = fTemp9;
			fRec23[0] = fVec22[(faust_wrap_sub(IOTA0, iConst37)) & 2047];
			float fTemp10 = fRec23[1] + 0.5f * (fRec22[1] - fTemp9);
			fVec23[IOTA0 & 1023] = fTemp10;
			fRec22[0] = fVec23[(faust_wrap_sub(IOTA0, iConst38)) & 1023];
			output1[i0] = static_cast<FAUSTFLOAT>(0.15f * (fRec22[1] - 0.5f * fTemp10));
			fRec6[1] = fRec6[0];
			fRec4[1] = fRec4[0];
			fRec7[1] = fRec7[0];
			IOTA0 = faust_wrap_add(IOTA0, 1);
			fRec5[1] = fRec5[0];
			fRec8[1] = fRec8[0];
			fRec9[1] = fRec9[0];
			fRec10[1] = fRec10[0];
			fRec11[1] = fRec11[0];
			fRec12[1] = fRec12[0];
			fRec13[1] = fRec13[0];
			fRec14[1] = fRec14[0];
			fRec15[1] = fRec15[0];
			fRec16[1] = fRec16[0];
			fRec17[1] = fRec17[0];
			fRec18[1] = fRec18[0];
			fRec19[1] = fRec19[0];
			fRec20[1] = fRec20[0];
			fRec21[1] = fRec21[0];
			fRec3[1] = fRec3[0];
			fRec2[1] = fRec2[0];
			fRec1[1] = fRec1[0];
			fRec0[1] = fRec0[0];
			fRec26[1] = fRec26[0];
			fRec27[1] = fRec27[0];
			fRec28[1] = fRec28[0];
			fRec29[1] = fRec29[0];
			fRec30[1] = fRec30[0];
			fRec31[1] = fRec31[0];
			fRec32[1] = fRec32[0];
			fRec33[1] = fRec33[0];
			fRec34[1] = fRec34[0];
			fRec35[1] = fRec35[0];
			fRec36[1] = fRec36[0];
			fRec37[1] = fRec37[0];
			fRec38[1] = fRec38[0];
			fRec39[1] = fRec39[0];
			fRec40[1] = fRec40[0];
			fRec41[1] = fRec41[0];
			fRec25[1] = fRec25[0];
			fRec24[1] = fRec24[0];
			fRec23[1] = fRec23[0];
			fRec22[1] = fRec22[0];
		}
	}

};

#endif
