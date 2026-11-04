/* ------------------------------------------------------------
name: "NoiseGate"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFxNoiseGate -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __NyxFxNoiseGate_H__
#define  __NyxFxNoiseGate_H__

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
#define FAUSTCLASS NyxFxNoiseGate
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


class NyxFxNoiseGate : public dsp {

 private:

	FAUSTFLOAT fHslider0;
	FAUSTFLOAT fHslider1;
	int fSampleRate;
	float fConst0;
	float fConst1;
	float fRec1[2];
	FAUSTFLOAT fHslider2;
	int iVec0[2];
	float fConst2;
	FAUSTFLOAT fHslider3;
	int iRec2[2];
	float fRec0[2];

 public:
	NyxFxNoiseGate() {
	}

	NyxFxNoiseGate(const NyxFxNoiseGate&) = default;

	virtual ~NyxFxNoiseGate() = default;

	NyxFxNoiseGate& operator=(const NyxFxNoiseGate&) = default;

	void metadata(Meta* m) {
		m->declare("analyzers.lib/amp_follower_ar:author", "Jonatan Liljedahl, revised by Romain Michon");
		m->declare("analyzers.lib/name", "Faust Analyzer Library");
		m->declare("analyzers.lib/version", "1.4.0");
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.23.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFxNoiseGate -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("filename", "NoiseGate.dsp");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LicenseRef-LGPL-2.1-or-later-with-Faust-exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("misceffects.lib/gate_gain_mono:author", "Julius O. Smith III");
		m->declare("misceffects.lib/gate_gain_mono:license", "LicenseRef-STK-4.3");
		m->declare("misceffects.lib/gate_stereo:author", "Julius O. Smith III");
		m->declare("misceffects.lib/gate_stereo:license", "LicenseRef-STK-4.3");
		m->declare("misceffects.lib/name", "Misc Effects Library");
		m->declare("misceffects.lib/version", "2.6.0");
		m->declare("name", "NoiseGate");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("signals.lib/name", "Faust Routing Library");
		m->declare("signals.lib/onePoleSwitching:author", "Jonatan Liljedahl, revised by Dario Sanfilippo");
		m->declare("signals.lib/onePoleSwitching:licence", "LicenseRef-STK-4.3");
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
		fConst1 = 1.0f / fConst0;
		fConst2 = 0.001f * fConst0;
	}

	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(2.0f);
		fHslider1 = static_cast<FAUSTFLOAT>(1e+02f);
		fHslider2 = static_cast<FAUSTFLOAT>(-45.0f);
		fHslider3 = static_cast<FAUSTFLOAT>(5e+01f);
	}

	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = faust_wrap_add(l0, 1)) {
			fRec1[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 2; l1 = faust_wrap_add(l1, 1)) {
			iVec0[l1] = 0;
		}
		for (int l2 = 0; l2 < 2; l2 = faust_wrap_add(l2, 1)) {
			iRec2[l2] = 0;
		}
		for (int l3 = 0; l3 < 2; l3 = faust_wrap_add(l3, 1)) {
			fRec0[l3] = 0.0f;
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

	virtual NyxFxNoiseGate* clone() {
		return new NyxFxNoiseGate(*this);
	}

	virtual int getSampleRate() {
		return fSampleRate;
	}

	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("NoiseGate");
		ui_interface->addHorizontalSlider("attack_ms", &fHslider0, FAUSTFLOAT(2.0f), FAUSTFLOAT(0.1f), FAUSTFLOAT(5e+02f), FAUSTFLOAT(0.1f));
		ui_interface->addHorizontalSlider("hold_ms", &fHslider3, FAUSTFLOAT(5e+01f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+03f), FAUSTFLOAT(1.0f));
		ui_interface->addHorizontalSlider("release_ms", &fHslider1, FAUSTFLOAT(1e+02f), FAUSTFLOAT(1.0f), FAUSTFLOAT(3e+03f), FAUSTFLOAT(1.0f));
		ui_interface->addHorizontalSlider("threshold_dB", &fHslider2, FAUSTFLOAT(-45.0f), FAUSTFLOAT(-96.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(0.1f));
		ui_interface->closeBox();
	}

	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = 0.001f * static_cast<float>(fHslider0);
		float fSlow1 = 0.001f * static_cast<float>(fHslider1);
		float fSlow2 = std::min<float>(fSlow0, fSlow1);
		int iSlow3 = std::fabs(fSlow2) < 1.1920929e-07f;
		float fSlow4 = ((iSlow3) ? 0.0f : std::exp(-(fConst1 / ((iSlow3) ? 1.0f : fSlow2))));
		float fSlow5 = 1.0f - fSlow4;
		float fSlow6 = std::pow(1e+01f, 0.05f * static_cast<float>(fHslider2));
		int iSlow7 = static_cast<int>(fConst2 * static_cast<float>(fHslider3));
		int iSlow8 = std::fabs(fSlow1) < 1.1920929e-07f;
		float fSlow9 = ((iSlow8) ? 0.0f : std::exp(-(fConst1 / ((iSlow8) ? 1.0f : fSlow1))));
		int iSlow10 = std::fabs(fSlow0) < 1.1920929e-07f;
		float fSlow11 = ((iSlow10) ? 0.0f : std::exp(-(fConst1 / ((iSlow10) ? 1.0f : fSlow0))));
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			float fTemp0 = static_cast<float>(input0[i0]);
			float fTemp1 = static_cast<float>(input1[i0]);
			fRec1[0] = fSlow5 * std::fabs(std::fabs(fTemp0) + std::fabs(fTemp1)) + fSlow4 * fRec1[1];
			int iTemp2 = fRec1[0] > fSlow6;
			iVec0[0] = iTemp2;
			iRec2[0] = std::max<int>(faust_wrap_mul(iSlow7, iTemp2 < iVec0[1]), faust_wrap_add(-1, iRec2[1]));
			float fTemp3 = std::fabs(std::max<float>(static_cast<float>(iTemp2), static_cast<float>(iRec2[0] > 0)));
			float fTemp4 = ((fTemp3 > fRec0[1]) ? fSlow11 : fSlow9);
			fRec0[0] = fTemp3 * (1.0f - fTemp4) + fRec0[1] * fTemp4;
			output0[i0] = static_cast<FAUSTFLOAT>(fTemp0 * fRec0[0]);
			output1[i0] = static_cast<FAUSTFLOAT>(fTemp1 * fRec0[0]);
			fRec1[1] = fRec1[0];
			iVec0[1] = iVec0[0];
			iRec2[1] = iRec2[0];
			fRec0[1] = fRec0[0];
		}
	}

};

#endif
