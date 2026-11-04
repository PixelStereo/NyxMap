/* ------------------------------------------------------------
name: "Compressor"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFxCompressor -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __NyxFxCompressor_H__
#define  __NyxFxCompressor_H__

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
#define FAUSTCLASS NyxFxCompressor
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


class NyxFxCompressor : public dsp {

 private:

	FAUSTFLOAT fHslider0;
	int fSampleRate;
	float fConst0;
	float fConst1;
	FAUSTFLOAT fHslider1;
	FAUSTFLOAT fHslider2;
	float fRec1[2];
	FAUSTFLOAT fHslider3;
	float fRec0[2];
	float fConst2;
	FAUSTFLOAT fHslider4;
	float fConst3;
	float fRec2[2];

 public:
	NyxFxCompressor() {
	}

	NyxFxCompressor(const NyxFxCompressor&) = default;

	virtual ~NyxFxCompressor() = default;

	NyxFxCompressor& operator=(const NyxFxCompressor&) = default;

	void metadata(Meta* m) {
		m->declare("analyzers.lib/amp_follower_ar:author", "Jonatan Liljedahl, revised by Romain Michon");
		m->declare("analyzers.lib/name", "Faust Analyzer Library");
		m->declare("analyzers.lib/version", "1.4.0");
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.23.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn NyxFxCompressor -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("compressors.lib/compression_gain_mono:author", "Julius O. Smith III");
		m->declare("compressors.lib/compression_gain_mono:copyright", "Copyright (C) 2014-2020 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("compressors.lib/compression_gain_mono:license", "LicenseRef-STK-4.3");
		m->declare("compressors.lib/compressor_stereo:author", "Julius O. Smith III");
		m->declare("compressors.lib/compressor_stereo:copyright", "Copyright (C) 2014-2020 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("compressors.lib/compressor_stereo:license", "LicenseRef-STK-4.3");
		m->declare("compressors.lib/name", "Faust Compressor Effect Library");
		m->declare("compressors.lib/version", "1.6.0");
		m->declare("filename", "Compressor.dsp");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LicenseRef-LGPL-2.1-or-later-with-Faust-exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "Compressor");
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
		fConst2 = 44.1f / fConst0;
		fConst3 = 1.0f - fConst2;
	}

	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(1e+01f);
		fHslider1 = static_cast<FAUSTFLOAT>(4.0f);
		fHslider2 = static_cast<FAUSTFLOAT>(1e+02f);
		fHslider3 = static_cast<FAUSTFLOAT>(-18.0f);
		fHslider4 = static_cast<FAUSTFLOAT>(0.0f);
	}

	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = faust_wrap_add(l0, 1)) {
			fRec1[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 2; l1 = faust_wrap_add(l1, 1)) {
			fRec0[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 2; l2 = faust_wrap_add(l2, 1)) {
			fRec2[l2] = 0.0f;
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

	virtual NyxFxCompressor* clone() {
		return new NyxFxCompressor(*this);
	}

	virtual int getSampleRate() {
		return fSampleRate;
	}

	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("Compressor");
		ui_interface->addHorizontalSlider("attack_ms", &fHslider0, FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.1f), FAUSTFLOAT(5e+02f), FAUSTFLOAT(0.1f));
		ui_interface->addHorizontalSlider("makeup_dB", &fHslider4, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(24.0f), FAUSTFLOAT(0.1f));
		ui_interface->addHorizontalSlider("ratio", &fHslider1, FAUSTFLOAT(4.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(2e+01f), FAUSTFLOAT(0.1f));
		ui_interface->addHorizontalSlider("release_ms", &fHslider2, FAUSTFLOAT(1e+02f), FAUSTFLOAT(1.0f), FAUSTFLOAT(3e+03f), FAUSTFLOAT(1.0f));
		ui_interface->addHorizontalSlider("threshold_dB", &fHslider3, FAUSTFLOAT(-18.0f), FAUSTFLOAT(-6e+01f), FAUSTFLOAT(0.0f), FAUSTFLOAT(0.1f));
		ui_interface->closeBox();
	}

	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = static_cast<float>(fHslider0);
		float fSlow1 = 0.0005f * fSlow0;
		int iSlow2 = std::fabs(fSlow1) < 1.1920929e-07f;
		float fSlow3 = ((iSlow2) ? 0.0f : std::exp(-(fConst1 / ((iSlow2) ? 1.0f : fSlow1))));
		float fSlow4 = (1.0f - fSlow3) * (1.0f / std::max<float>(1.1920929e-07f, static_cast<float>(fHslider1)) + -1.0f);
		float fSlow5 = 0.001f * static_cast<float>(fHslider2);
		int iSlow6 = std::fabs(fSlow5) < 1.1920929e-07f;
		float fSlow7 = ((iSlow6) ? 0.0f : std::exp(-(fConst1 / ((iSlow6) ? 1.0f : fSlow5))));
		float fSlow8 = 0.001f * fSlow0;
		int iSlow9 = std::fabs(fSlow8) < 1.1920929e-07f;
		float fSlow10 = ((iSlow9) ? 0.0f : std::exp(-(fConst1 / ((iSlow9) ? 1.0f : fSlow8))));
		float fSlow11 = static_cast<float>(fHslider3);
		float fSlow12 = fConst2 * static_cast<float>(fHslider4);
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			float fTemp0 = static_cast<float>(input0[i0]);
			float fTemp1 = static_cast<float>(input1[i0]);
			float fTemp2 = std::fabs(std::fabs(fTemp0) + std::fabs(fTemp1));
			float fTemp3 = ((fTemp2 > fRec1[1]) ? fSlow10 : fSlow7);
			fRec1[0] = fTemp2 * (1.0f - fTemp3) + fRec1[1] * fTemp3;
			fRec0[0] = fSlow4 * std::max<float>(2e+01f * std::log10(std::max<float>(1.1754944e-38f, fRec1[0])) - fSlow11, 0.0f) + fSlow3 * fRec0[1];
			float fTemp4 = std::pow(1e+01f, 0.05f * fRec0[0]);
			fRec2[0] = fSlow12 + fConst3 * fRec2[1];
			float fTemp5 = std::pow(1e+01f, 0.05f * fRec2[0]);
			output0[i0] = static_cast<FAUSTFLOAT>(fTemp0 * fTemp4 * fTemp5);
			output1[i0] = static_cast<FAUSTFLOAT>(fTemp1 * fTemp4 * fTemp5);
			fRec1[1] = fRec1[0];
			fRec0[1] = fRec0[0];
			fRec2[1] = fRec2[0];
		}
	}

};

#endif
