// Copyright (C) 2010-2016  Lukas Lalinsky
// Distributed under the MIT license, see the LICENSE file for details.

#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <chromaprint.h>
#include "fingerprinter.h"
#include "fingerprint_compressor.h"
#include "fingerprint_decompressor.h"
#include "fingerprint_matcher.h"
#include "fingerprinter_configuration.h"
#include "base64.h"
#include "simhash.h"
#include "debug.h"

struct ChromaprintContextPrivate {
	ChromaprintContextPrivate(int algorithm)
		: algorithm(algorithm),
		  fingerprinter(chromaprint::CreateFingerprinterConfiguration(algorithm)) {}
	bool finished = false;
	int algorithm;
	chromaprint::Fingerprinter fingerprinter;
	std::vector<uint32_t> fingerprint;
};

struct ChromaprintMatcherContextPrivate {
	ChromaprintMatcherContextPrivate(int algorithm)
		: algorithm(algorithm),
		  matcher(chromaprint::CreateFingerprinterConfiguration(algorithm)) {}
	int algorithm;
	chromaprint::FingerprintMatcher matcher;
	std::vector<uint32_t> fp[2];
};

extern "C" {

namespace chromaprint {

#define FAIL_IF(x, msg) if (x) { DEBUG(msg); return 0; }

#define STR(x) #x
#define VERSION_STR(major, minor, patch) \
	STR(major) "." STR(minor) "." STR(patch)

static const char *version_str = VERSION_STR(
	CHROMAPRINT_VERSION_MAJOR,
	CHROMAPRINT_VERSION_MINOR,
	CHROMAPRINT_VERSION_PATCH);

const char *chromaprint_get_version(void)
{
	return version_str;
}

ChromaprintContext *chromaprint_new(int algorithm)
{
	ChromaprintContextPrivate *ctx = new ChromaprintContextPrivate(algorithm);
	return (ChromaprintContext *) ctx;
}

void chromaprint_free(ChromaprintContext *c)
{
	ChromaprintContextPrivate *ctx = (ChromaprintContextPrivate *) c;
	delete ctx;
}

int chromaprint_set_option(ChromaprintContext *c, const char *name, int value)
{
	ChromaprintContextPrivate *ctx = (ChromaprintContextPrivate *) c;
	return ctx->fingerprinter.SetOption(name, value) ? 1 : 0;
}

int chromaprint_start(ChromaprintContext *c, int sample_rate, int num_channels)
{
	ChromaprintContextPrivate *ctx = (ChromaprintContextPrivate *) c;
	ctx->finished = false;
	return ctx->fingerprinter.Start(sample_rate, num_channels) ? 1 : 0;
}

int chromaprint_feed(ChromaprintContext *c, const int16_t *data, int length)
{
	ChromaprintContextPrivate *ctx = (ChromaprintContextPrivate *) c;
	ctx->fingerprinter.Consume(data, length);
	return 1;
}

int chromaprint_finish(ChromaprintContext *c)
{
	ChromaprintContextPrivate *ctx = (ChromaprintContextPrivate *) c;
	ctx->fingerprint = ctx->fingerprinter.Finish();
	ctx->finished = true;
	return 1;
}

int chromaprint_get_fingerprint(ChromaprintContext *c, char **data)
{
	ChromaprintContextPrivate *ctx = (ChromaprintContextPrivate *) c;
	if (!ctx->finished) {
		return 0;
	}
	std::string fingerprint = CompressFingerprint(ctx->fingerprint, ctx->algorithm);
	*data = (char *) malloc(GetBase64EncodedSize(fingerprint.size()) + 1);
	Base64Encode(fingerprint.begin(), fingerprint.end(), *data, true);
	return 1;
}

int chromaprint_get_raw_fingerprint(ChromaprintContext *c, uint32_t **data, int *size)
{
	ChromaprintContextPrivate *ctx = (ChromaprintContextPrivate *) c;
	if (!ctx->finished) {
		return 0;
	}
	*data = (uint32_t *) malloc(sizeof(uint32_t) * ctx->fingerprint.size());
	if (!*data) {
		return 0;
	}
	*size = ctx->fingerprint.size();
	copy(ctx->fingerprint.begin(), ctx->fingerprint.end(), *data);
	return 1;
}

int chromaprint_get_fingerprint_hash(ChromaprintContext *c, uint32_t *hash)
{
	ChromaprintContextPrivate *ctx = (ChromaprintContextPrivate *) c;
	if (!ctx->finished) {
		return 0;
	}
	*hash = SimHash(ctx->fingerprint);
	return 1;
}

int chromaprint_encode_fingerprint(const uint32_t *fp, int size, int algorithm, char **encoded_fp, int *encoded_size, int base64)
{
	std::vector<uint32_t> uncompressed(fp, fp + size);
	std::string encoded = chromaprint::CompressFingerprint(uncompressed, algorithm);
	if (base64) {
		encoded = chromaprint::Base64Encode(encoded);
	}
	*encoded_fp = (char *) malloc(encoded.size() + 1);
	*encoded_size = encoded.size();	
	memcpy(*encoded_fp, encoded.c_str(), encoded.size() + 1);
	return 1;
}

int chromaprint_decode_fingerprint(const char *encoded_fp, int encoded_size, uint32_t **fp, int *size, int *algorithm, int base64)
{
	std::string encoded(encoded_fp, encoded_size);
	if (base64) {
		encoded = chromaprint::Base64Decode(encoded);
	}
	std::vector<uint32_t> uncompressed = chromaprint::DecompressFingerprint(encoded, algorithm);
	*fp = (uint32_t *) malloc(sizeof(uint32_t) * uncompressed.size());
	*size = uncompressed.size();
	std::copy(uncompressed.begin(), uncompressed.end(), *fp);
	return 1;
}

int chromaprint_hash_fingerprint(const uint32_t *fp, int size, uint32_t *hash)
{
	if (fp == NULL || size < 0 || hash == NULL) {
		return 0;
	}
	*hash = SimHash(fp, size);
	return 1;
}

ChromaprintMatcherContext *chromaprint_matcher_new(int version)
{
	return new ChromaprintMatcherContext(version);
}

void chromaprint_matcher_free(ChromaprintMatcherContext *ctx)
{
	delete ctx;
}

int chromaprint_matcher_set_fingerprint(ChromaprintMatcherContext *ctx, int idx, const char *fp)
{
	FAIL_IF(!ctx, "context can't be NULL");
	FAIL_IF(idx < 0 || idx > 1, "idx can be only 0 or 1");

	int algorithm;
	ctx->fp[idx] = DecompressFingerprint(Base64Decode(fp), &algorithm);
	FAIL_IF(algorithm != ctx->algorithm, "invalid fingerprint algorithm");
	return 1;
}

int chromaprint_matcher_set_raw_fingerprint(ChromaprintMatcherContext *ctx, int idx, const uint32_t *fp, int size)
{
	FAIL_IF(!ctx, "context can't be NULL");
	FAIL_IF(idx < 0 || idx > 1, "idx can be only 0 or 1");

	ctx->fp[idx].assign(fp, fp + size);
	return 1;
}

int chromaprint_matcher_run(ChromaprintMatcherContext *ctx)
{
	FAIL_IF(!ctx, "context can't be NULL");
	FAIL_IF(ctx->fp[0].empty(), "fingerprint 0 is empty");
	FAIL_IF(ctx->fp[1].empty(), "fingerprint 1 is empty");

	return ctx->matcher.Match(ctx->fp[0], ctx->fp[1]) ? 1 : 0;
}

int chromaprint_matcher_get_num_segments(ChromaprintMatcherContext *ctx, int *num_segments)
{
	FAIL_IF(!ctx, "context can't be NULL");
	FAIL_IF(!num_segments, "num_segments can't be NULL");

	*num_segments = ctx->matcher.segments().size();
	return 1;
}

int chromaprint_matcher_get_segment_position(ChromaprintMatcherContext *ctx, int idx, int *pos1, int *pos2, int *duration)
{
	FAIL_IF(!ctx, "context can't be NULL");

	const auto &segments = ctx->matcher.segments();
	FAIL_IF(idx < 0 || idx >= segments.size(), "invalid idx");

	*pos1 = segments[idx].pos1;
	*pos2 = segments[idx].pos2;
	*duration = segments[idx].duration;
	return 1;
}

int chromaprint_matcher_get_segment_position_ms(ChromaprintMatcherContext *ctx, int idx, int *pos1, int *pos2, int *duration)
{
	FAIL_IF(!ctx, "context can't be NULL");

	const auto &segments = ctx->matcher.segments();
	FAIL_IF(idx < 0 || idx >= segments.size(), "invalid idx");

	*pos1 = round(1000 * ctx->matcher.GetHashTime(segments[idx].pos1));
	*pos2 = round(1000 * ctx->matcher.GetHashTime(segments[idx].pos2));
	*duration = round(1000 * ctx->matcher.GetHashTime(segments[idx].duration));
	return 1;
}

int chromaprint_matcher_get_segment_score(ChromaprintMatcherContext *ctx, int idx, int *score)
{
	FAIL_IF(!ctx, "context can't be NULL");

	const auto &segments = ctx->matcher.segments();
	FAIL_IF(idx < 0 || idx >= segments.size(), "invalid idx");

	*score = std::max(0, std::min(100, static_cast<int>(round(100 * (1.0 - segments[idx].score / 32.0)))));
	return 1;
}

void chromaprint_dealloc(void *ptr)
{
	free(ptr);
}

}; // namespace chromaprint

}; // extern "C"
