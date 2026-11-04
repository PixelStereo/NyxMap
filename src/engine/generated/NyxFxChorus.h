/* ------------------------------------------------------------
name: "Chorus"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFxChorus -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __NyxFxChorus_H__
#define  __NyxFxChorus_H__

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
#define FAUSTCLASS NyxFxChorus
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

class NyxFxChorusSIG0 {

  private:

	int iVec2[2];
	int iRec1[2];
	int fSampleRate;

  public:

	int getNumInputsNyxFxChorusSIG0() {
		return 0;
	}
	int getNumOutputsNyxFxChorusSIG0() {
		return 1;
	}

	void instanceInitNyxFxChorusSIG0(int sample_rate) {
		fSampleRate = sample_rate;
		for (int l3 = 0; l3 < 2; l3 = faust_wrap_add(l3, 1)) {
			iVec2[l3] = 0;
		}
		for (int l4 = 0; l4 < 2; l4 = faust_wrap_add(l4, 1)) {
			iRec1[l4] = 0;
		}
	}

	void fillNyxFxChorusSIG0(int count, float* table) {
		for (int i1 = 0; i1 < count; i1 = faust_wrap_add(i1, 1)) {
			iVec2[0] = 1;
			iRec1[0] = (faust_wrap_add(iVec2[1], iRec1[1])) % 65536;
			table[i1] = std::sin(9.58738e-05f * static_cast<float>(iRec1[0]));
			iVec2[1] = iVec2[0];
			iRec1[1] = iRec1[0];
		}
	}

};

static NyxFxChorusSIG0* newNyxFxChorusSIG0() { return (NyxFxChorusSIG0*)new NyxFxChorusSIG0(); }
static void deleteNyxFxChorusSIG0(NyxFxChorusSIG0* dsp) { delete dsp; }

static float ftbl0NyxFxChorusSIG0[65536];

class NyxFxChorus : public dsp {

 private:

	int IOTA0;
	float fVec0[16384];
	int fSampleRate;
	float fConst0;
	float fConst1;
	float fConst2;
	FAUSTFLOAT fHslider0;
	float fConst3;
	int iVec1[2];
	float fRec0[2];
	float fConst4;
	FAUSTFLOAT fHslider1;
	float fRec3[2];
	float fRec2[2];
	float fVec3[16384];
	float fConst5;
	float fRec4[2];

 public:
	NyxFxChorus() {
	}

	NyxFxChorus(const NyxFxChorus&) = default;

	virtual ~NyxFxChorus() = default;

	NyxFxChorus& operator=(const NyxFxChorus&) = default;

	void metadata(Meta* m) {
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.23.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFxChorus -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("delays.lib/name", "Faust Delay Library");
		m->declare("delays.lib/version", "1.2.0");
		m->declare("filename", "Chorus.dsp");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LicenseRef-LGPL-2.1-or-later-with-Faust-exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "Chorus");
		m->declare("oscillators.lib/name", "Faust Oscillator Library");
		m->declare("oscillators.lib/version", "1.8.0");
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
		NyxFxChorusSIG0* sig0 = newNyxFxChorusSIG0();
		sig0->instanceInitNyxFxChorusSIG0(sample_rate);
		sig0->fillNyxFxChorusSIG0(65536, ftbl0NyxFxChorusSIG0);
		deleteNyxFxChorusSIG0(sig0);
	}

	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		fConst1 = 0.001f * fConst0;
		fConst2 = 44.1f / fConst0;
		fConst3 = 1.0f - fConst2;
		fConst4 = 1.0f / fConst0;
		fConst5 = 1.07f / fConst0;
	}

	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(4.0f);
		fHslider1 = static_cast<FAUSTFLOAT>(0.8f);
	}

	virtual void instanceClear() {
		IOTA0 = 0;
		for (int l0 = 0; l0 < 16384; l0 = faust_wrap_add(l0, 1)) {
			fVec0[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 2; l1 = faust_wrap_add(l1, 1)) {
			iVec1[l1] = 0;
		}
		for (int l2 = 0; l2 < 2; l2 = faust_wrap_add(l2, 1)) {
			fRec0[l2] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = faust_wrap_add(l5, 1)) {
			fRec3[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = faust_wrap_add(l6, 1)) {
			fRec2[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 16384; l7 = faust_wrap_add(l7, 1)) {
			fVec3[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 2; l8 = faust_wrap_add(l8, 1)) {
			fRec4[l8] = 0.0f;
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

	virtual NyxFxChorus* clone() {
		return new NyxFxChorus(*this);
	}

	virtual int getSampleRate() {
		return fSampleRate;
	}

	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("Chorus");
		ui_interface->addHorizontalSlider("depth_ms", &fHslider0, FAUSTFLOAT(4.0f), FAUSTFLOAT(0.1f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.1f));
		ui_interface->addHorizontalSlider("rate_Hz", &fHslider1, FAUSTFLOAT(0.8f), FAUSTFLOAT(0.05f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.01f));
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
			fVec0[IOTA0 & 16383] = fTemp0;
			iVec1[0] = 1;
			fRec0[0] = fSlow0 + fConst3 * fRec0[1];
			int iTemp1 = faust_wrap_sub(1, iVec1[1]);
			fRec3[0] = fSlow1 + fConst3 * fRec3[1];
			float fTemp2 = ((iTemp1) ? 0.0f : fRec2[1] + fConst4 * fRec3[0]);
			fRec2[0] = fTemp2 - std::floor(fTemp2);
			float fTemp3 = fConst1 * (12.0f + fRec0[0] * ftbl0NyxFxChorusSIG0[std::max<int>(0, std::min<int>(static_cast<int>(65536.0f * fRec2[0]), 65535))]);
			int iTemp4 = static_cast<int>(fTemp3);
			float fTemp5 = std::floor(fTemp3);
			output0[i0] = static_cast<FAUSTFLOAT>(fVec0[(faust_wrap_sub(IOTA0, std::min<int>(8193, std::max<int>(0, iTemp4)))) & 16383] * (1.0f - fTemp3 + fTemp5) + (fTemp3 - fTemp5) * fVec0[(faust_wrap_sub(IOTA0, std::min<int>(8193, std::max<int>(0, faust_wrap_add(1, iTemp4))))) & 16383]);
			float fTemp6 = static_cast<float>(input1[i0]);
			fVec3[IOTA0 & 16383] = fTemp6;
			float fTemp7 = ((iTemp1) ? 0.0f : fRec4[1] + fConst5 * fRec3[0]);
			fRec4[0] = fTemp7 - std::floor(fTemp7);
			float fTemp8 = fConst1 * (12.0f + fRec0[0] * ftbl0NyxFxChorusSIG0[std::max<int>(0, std::min<int>(static_cast<int>(65536.0f * fRec4[0]), 65535))]);
			int iTemp9 = static_cast<int>(fTemp8);
			float fTemp10 = std::floor(fTemp8);
			output1[i0] = static_cast<FAUSTFLOAT>(fVec3[(faust_wrap_sub(IOTA0, std::min<int>(8193, std::max<int>(0, iTemp9)))) & 16383] * (1.0f - fTemp8 + fTemp10) + (fTemp8 - fTemp10) * fVec3[(faust_wrap_sub(IOTA0, std::min<int>(8193, std::max<int>(0, faust_wrap_add(1, iTemp9))))) & 16383]);
			IOTA0 = faust_wrap_add(IOTA0, 1);
			iVec1[1] = iVec1[0];
			fRec0[1] = fRec0[0];
			fRec3[1] = fRec3[0];
			fRec2[1] = fRec2[0];
			fRec4[1] = fRec4[0];
		}
	}

};

#endif
