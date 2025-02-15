#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>

/// Structure representing a biquad (second‑order section) filter in float precision.
/// The transfer function is:
///   H(z) = (b0 + b1 z⁻¹ + b2 z⁻²) / (1 + a1 z⁻¹ + a2 z⁻²)
struct BiquadFloat
{
	float b0, b1, b2; ///< Numerator coefficients.
	float a1, a2;	  ///< Denominator coefficients (a0 is assumed to be 1).
};

/**
	@brief Butterworth low‑pass filter using cascaded biquad sections (float version).

	This class designs a Butterworth low‑pass filter of a given order and normalized
	cutoff frequency (0 < normalCutoff < 1, with 1 corresponding to Nyquist) by computing
	the necessary biquad sections. It provides methods for cascade filtering and for
	zero‑phase filtering (forward–reverse filtering) on float‑precision signals.
*/
class ButterworthLowpass
{
public:
	/**
		@brief Constructs a ButterworthLowpass with the specified order and normalized cutoff.

		@param order The filter order.
		@param normalCutoff The normalized cutoff frequency (0 < normalCutoff < 1).
	*/
	ButterworthLowpass(int order, float normalCutoff)
		: m_order(order)
		, m_normalCutoff(normalCutoff)
	{
		designFilter();
	}

	/**
		@brief Filters the input signal through the cascaded biquad sections.

		@param data The input signal.
		@return The filtered signal.
	*/
	void filter(std::vector<float> &data) const
	{
		// Cascade the data through each biquad section.
		for (size_t s = 0; s < m_sections.size(); s++) {
			data = filterBiquad(m_sections[s], data);
		}
	}

	/**
		@brief Applies zero‑phase filtering (filtfilt) to the input signal.

		The filter is applied forward, then the signal is reversed and filtered again,
		then reversed to yield a zero‑phase response.

		@param data The input signal.
		@return The zero‑phase filtered signal.
	*/
	void filtfilt(std::vector<float> &data) const
	{
		filter(data);
		std::reverse(data.begin(), data.end());
		filter(data);
		std::reverse(data.begin(), data.end());
	}

private:
	/**
		@brief Designs the Butterworth filter and computes the SOS (biquad) sections.

		The design uses the bilinear transform. First the analog Butterworth poles
		are computed (with prewarping), then mapped to the digital domain, and finally
		grouped into second‑order sections. For odd orders, a first‑order section is used.
	*/
	void designFilter()
	{
		// Prewarp the digital cutoff frequency (T = 1 assumed)
		float wc = tanf(static_cast<float>(M_PI) * m_normalCutoff);
		// Compute analog Butterworth poles: s_k = wc * exp(j*(pi/2 + (2k+1)*pi/(2*m_order)))
		std::vector<std::complex<float>> analogPoles;
		for (int k = 0; k < m_order; k++) {
			float theta = static_cast<float>(M_PI) * (2 * k + 1) / (2.0f * m_order);
			std::complex<float> s = wc * std::exp(std::complex<float>(0.0f, static_cast<float>(M_PI) / 2.0f + theta));
			analogPoles.push_back(s);
		}
		// Map analog poles to digital via the bilinear transform: z = (2 + s) / (2 - s)
		std::vector<std::complex<float>> digitalPoles;
		for (size_t i = 0; i < analogPoles.size(); i++) {
			std::complex<float> z = (2.0f + analogPoles[i]) / (2.0f - analogPoles[i]);
			digitalPoles.push_back(z);
		}
		// All zeros are at z = -1.
		// Group poles into second‑order sections (SOS). For even order, we have order/2 sections.
		// For odd order, one real pole remains (handled as a first‑order section).
		std::vector<std::complex<float>> complexPoles = digitalPoles;
		std::vector<BiquadFloat> sections;
		if (m_order % 2 == 1) {
			// Odd order: remove one real pole.
			int realIndex = -1;
			for (size_t i = 0; i < complexPoles.size(); i++) {
				if (fabs(complexPoles[i].imag()) < 1e-6f) {
					realIndex = static_cast<int>(i);
					break;
				}
			}
			if (realIndex < 0) {
				// Fallback if no real pole is found.
				realIndex = 0;
			}
			std::complex<float> pole = complexPoles[realIndex];
			complexPoles.erase(complexPoles.begin() + realIndex);
			// First‑order section: Denom: 1 - p z⁻¹, Numerator: (1 + z⁻¹)
			BiquadFloat sec;
			float a0 = 1.0f;
			float a1 = -pole.real(); // pole is real
			float b0 = 1.0f, b1 = 1.0f;
			// Gain correction: enforce unity gain at DC.
			float G = (b0 + b1) / (a0 + a1);
			b0 *= G;
			b1 *= G;
			sec.b0 = b0;
			sec.b1 = b1;
			sec.b2 = 0.0f;
			sec.a1 = a1;
			sec.a2 = 0.0f;
			sections.push_back(sec);
		}
		// The remaining poles come in conjugate pairs.
		for (size_t i = 0; i < complexPoles.size(); i += 2) {
			// Use two poles that should be complex conjugates.
			std::complex<float> p1 = complexPoles[i];
			// std::complex<float> p2 = complexPoles[i + 1];
			// Denominator polynomial: 1 - (p1+p2) z⁻¹ + (p1*p2) z⁻².
			float a0 = 1.0f;
			float a1 = -2.0f * p1.real(); // since p1 + p2 = 2 Re(p1)
			float a2 = std::norm(p1);	  // p1*p2 = |p1|²
			// Numerator polynomial: zeros at z = -1 (double zero) -> (1 + z⁻¹)² = 1 + 2z⁻¹ + z⁻².
			float b0 = 1.0f, b1 = 2.0f, b2 = 1.0f;
			// Gain correction for unity DC gain.
			float G = (b0 + b1 + b2) / (a0 + a1 + a2);
			b0 *= G;
			b1 *= G;
			b2 *= G;
			BiquadFloat sec;
			sec.b0 = b0;
			sec.b1 = b1;
			sec.b2 = b2;
			sec.a1 = a1;
			sec.a2 = a2;
			sections.push_back(sec);
		}
		m_sections = sections;
	}

	/**
		@brief Filters the input data through a single biquad section.

		The difference equation is:
		    y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]

		@param section The biquad section.
		@param data The input signal.
		@return The filtered signal.
	*/
	static std::vector<float> filterBiquad(const BiquadFloat &section, const std::vector<float> &data)
	{
		std::vector<float> y(data.size(), 0.0f);
		float x_n, x_n1 = 0.0f, x_n2 = 0.0f;
		float y_n1 = 0.0f, y_n2 = 0.0f;
		for (size_t n = 0; n < data.size(); n++) {
			x_n = data[n];
			float yn = section.b0 * x_n + section.b1 * x_n1 + section.b2 * x_n2 - section.a1 * y_n1 - section.a2 * y_n2;
			y[n] = yn;
			// Update delays.
			x_n2 = x_n1;
			x_n1 = x_n;
			y_n2 = y_n1;
			y_n1 = yn;
		}
		return y;
	}

	// Member variables
	int m_order;						 ///< Filter order.
	float m_normalCutoff;				 ///< Normalized cutoff frequency (0 < cutoff < 1).
	std::vector<BiquadFloat> m_sections; ///< Computed biquad sections.
};
