/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cstddef>
#include <cstdlib>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>
#include <random>
#include <vector>

#include <cpuinfo.h>
#include <qnnpack/AlignedAllocator.h>
#include <qnnpack/params.h>
#include <qnnpack/pack.h>
#include <qnnpack/q8gemm.h>
#include <qnnpack/q8conv.h>
#include <qnnpack/scalar-utils.h>
#include <qnnpack/requantization.h>
#include <qnnpack/sgemm.h>

#include <fp16.h>


class UVAddMicrokernelTester {
 public:
  inline UVAddMicrokernelTester& n(size_t n) {
    assert(n != 0);
    this->n_ = n;
    return *this;
  }

  inline size_t n() const {
    return this->n_;
  }

  inline UVAddMicrokernelTester& inplaceA(bool inplaceA) {
    this->inplaceA_ = inplaceA;
    return *this;
  }

  inline bool inplaceA() const {
    return this->inplaceA_;
  }

  inline UVAddMicrokernelTester& inplaceB(bool inplaceB) {
    this->inplaceB_ = inplaceB;
    return *this;
  }

  inline bool inplaceB() const {
    return this->inplaceB_;
  }

  inline UVAddMicrokernelTester& aScale(float aScale) {
    assert(aScale > 0.0f);
    assert(std::isnormal(aScale));
    this->aScale_ = aScale;
    return *this;
  }

  inline float aScale() const {
    return this->aScale_;
  }

  inline UVAddMicrokernelTester& aZeroPoint(uint8_t aZeroPoint) {
    this->aZeroPoint_ = aZeroPoint;
    return *this;
  }

  inline uint8_t aZeroPoint() const {
    return this->aZeroPoint_;
  }

  inline UVAddMicrokernelTester& bScale(float bScale) {
    assert(bScale > 0.0f);
    assert(std::isnormal(bScale));
    this->bScale_ = bScale;
    return *this;
  }

  inline float bScale() const {
    return this->bScale_;
  }

  inline UVAddMicrokernelTester& bZeroPoint(uint8_t bZeroPoint) {
    this->bZeroPoint_ = bZeroPoint;
    return *this;
  }

  inline uint8_t bZeroPoint() const {
    return this->bZeroPoint_;
  }

  inline UVAddMicrokernelTester& yScale(float yScale) {
    assert(yScale > 0.0f);
    assert(std::isnormal(yScale));
    this->yScale_ = yScale;
    return *this;
  }

  inline float yScale() const {
    return this->yScale_;
  }

  inline UVAddMicrokernelTester& yZeroPoint(uint8_t yZeroPoint) {
    this->yZeroPoint_ = yZeroPoint;
    return *this;
  }

  inline uint8_t yZeroPoint() const {
    return this->yZeroPoint_;
  }

  inline UVAddMicrokernelTester& qmin(uint8_t qmin) {
    this->qmin_ = qmin;
    return *this;
  }

  inline uint8_t qmin() const {
    return this->qmin_;
  }

  inline UVAddMicrokernelTester& qmax(uint8_t qmax) {
    this->qmax_ = qmax;
    return *this;
  }

  inline uint8_t qmax() const {
    return this->qmax_;
  }

  inline UVAddMicrokernelTester& iterations(size_t iterations) {
    this->iterations_ = iterations;
    return *this;
  }

  inline size_t iterations() const {
    return this->iterations_;
  }

  void test(q8uvadd_ukernel_function q8uvadd) const {
    std::random_device randomDevice;
    auto rng = std::mt19937(randomDevice());
    auto u8rng = std::bind(std::uniform_int_distribution<uint8_t>(), rng);

    std::vector<uint8_t> a(n());
    std::vector<uint8_t> b(n());
    std::vector<uint8_t> y(n());
    std::vector<float> yFP(n());
    std::vector<uint8_t> yRef(n());
    for (size_t iteration = 0; iteration < iterations(); iteration++) {
      std::generate(a.begin(), a.end(), std::ref(u8rng));
      std::generate(b.begin(), b.end(), std::ref(u8rng));
      if (inplaceA() || inplaceB()) {
        std::generate(y.begin(), y.end(), std::ref(u8rng));
      } else {
        std::fill(y.begin(), y.end(), 0xA5);
      }
      const uint8_t* aData = inplaceA() ? y.data() : a.data();
      const uint8_t* bData = inplaceB() ? y.data() : b.data();

      if (n() > 3) {
        ASSERT_NE(*std::max_element(a.cbegin(), a.cend()), *std::min_element(a.cbegin(), a.cend()));
        ASSERT_NE(*std::max_element(b.cbegin(), b.cend()), *std::min_element(b.cbegin(), b.cend()));
      }

      /* Prepare quantization parameters */
      const union qnnp_add_quantization_params quantizationParams =
          qnnp_compute_add_quantization_params(
            aZeroPoint(), bZeroPoint(), yZeroPoint(),
            aScale() / yScale(), bScale() / yScale(),
            qmin(), qmax());
      const union qnnp_add_quantization_params scalarQuantizationParams =
          qnnp_compute_scalar_add_quantization_params(
            aZeroPoint(), bZeroPoint(), yZeroPoint(),
            aScale() / yScale(), bScale() / yScale(),
            qmin(), qmax());

      /* Compute reference results */
      for (size_t i = 0; i < n(); i++) {
        yFP[i] = float(yZeroPoint()) +
          float(int32_t(aData[i]) - int32_t(aZeroPoint())) * (aScale() / yScale()) +
          float(int32_t(bData[i]) - int32_t(bZeroPoint())) * (bScale() / yScale());
        yFP[i] = std::min<float>(yFP[i], float(qmax()));
        yFP[i] = std::max<float>(yFP[i], float(qmin()));
        yRef[i] = qnnp_add_quantize(aData[i], bData[i], scalarQuantizationParams);
      }

      /* Call optimized micro-kernel */
      q8uvadd(n(), aData, bData, y.data(), &quantizationParams);

      /* Verify results */
      for (size_t i = 0; i < n(); i++) {
        ASSERT_LE(uint32_t(y[i]), uint32_t(qmax()))
          << "at " << i << ", n = " << n();
        ASSERT_GE(uint32_t(y[i]), uint32_t(qmin()))
          << "at " << i << ", n = " << n();
        ASSERT_NEAR(float(int32_t(y[i])), yFP[i], 0.6f)
          << "at " << i << ", n = " << n();
        ASSERT_EQ(uint32_t(yRef[i]), uint32_t(y[i]))
          << "at " << i << ", n = " << n();
      }
    }
  }

 private:
  size_t n_{1};
  bool inplaceA_{false};
  bool inplaceB_{false};
  float aScale_{0.75f};
  float bScale_{1.25f};
  float yScale_{0.96875f};
  uint8_t aZeroPoint_{121};
  uint8_t bZeroPoint_{127};
  uint8_t yZeroPoint_{133};
  uint8_t qmin_{0};
  uint8_t qmax_{255};
  size_t iterations_{15};
};
