




















#pragma warning(disable:4146)

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>
#include "pngout.hpp"

#pragma warning(disable:C4146)

using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::size_t;


TinyPngOut::TinyPngOut(uint32_t w, uint32_t h, std::ostream &out) :

		width(w),
		height(h),
		output(out),
		positionX(0),
		positionY(0),
		deflateFilled(0),
		adler(1) {


	if (width == 0 || height == 0)
		throw std::domain_error("Zero width or height");


	uint64_t lineSz = static_cast<uint64_t>(width) * 3 + 1;
	if (lineSz > UINT32_MAX)
		throw std::length_error("Image too large");
	lineSize = static_cast<uint32_t>(lineSz);

	uint64_t uncompRm = lineSize * height;
	if (uncompRm > UINT32_MAX)
		throw std::length_error("Image too large");
	uncompRemain = static_cast<uint32_t>(uncompRm);

	uint32_t numBlocks = uncompRemain / DEFLATE_MAX_BLOCK_SIZE;
	if (uncompRemain % DEFLATE_MAX_BLOCK_SIZE != 0)
		numBlocks++;

	uint64_t idatSize = static_cast<uint64_t>(numBlocks) * 5 + 6;
	idatSize += uncompRemain;
	if (idatSize > static_cast<uint32_t>(INT32_MAX))
		throw std::length_error("Image too large");


	uint8_t header[] = {

		0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,

		0x00, 0x00, 0x00, 0x0D,
		0x49, 0x48, 0x44, 0x52,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0x08, 0x02, 0x00, 0x00, 0x00,
		0, 0, 0, 0,

		0, 0, 0, 0,
		0x49, 0x44, 0x41, 0x54,

		0x08, 0x1D,
	};
	putBigUint32(width, &header[16]);
	putBigUint32(height, &header[20]);
	putBigUint32(idatSize, &header[33]);
	crc = 0;
	crc32(&header[12], 17);
	putBigUint32(crc, &header[29]);
	write(header);

	crc = 0;
	crc32(&header[37], 6);
}


void TinyPngOut::write(const uint8_t pixels[], size_t count) {
	if (count > SIZE_MAX / 3)
		throw std::length_error("Invalid argument");
	count *= 3;
	while (count > 0) {
		if (pixels == nullptr)
			throw std::invalid_argument("Null pointer");
		if (positionY >= height)
			throw std::logic_error("All image pixels already written");

		if (deflateFilled == 0) {
			uint16_t size = DEFLATE_MAX_BLOCK_SIZE;
			if (uncompRemain < size)
				size = static_cast<uint16_t>(uncompRemain);
			const uint8_t header[] = {
				static_cast<uint8_t>(uncompRemain <= DEFLATE_MAX_BLOCK_SIZE ? 1 : 0),
				static_cast<uint8_t>(size >> 0),
				static_cast<uint8_t>(size >> 8),
				static_cast<uint8_t>((size >> 0) ^ 0xFF),
				static_cast<uint8_t>((size >> 8) ^ 0xFF),
			};
			write(header);
			crc32(header, sizeof(header) / sizeof(header[0]));
		}
		assert(positionX < lineSize && deflateFilled < DEFLATE_MAX_BLOCK_SIZE);

		if (positionX == 0) {
			uint8_t b[] = {0};
			write(b);
			crc32(b, 1);
			adler32(b, 1);
			positionX++;
			uncompRemain--;
			deflateFilled++;

		} else {
			uint16_t n = DEFLATE_MAX_BLOCK_SIZE - deflateFilled;
			if (lineSize - positionX < n)
				n = static_cast<uint16_t>(lineSize - positionX);
			if (count < n)
				n = static_cast<uint16_t>(count);
			if (static_cast<std::make_unsigned<std::streamsize>::type>(std::numeric_limits<std::streamsize>::max()) < std::numeric_limits<decltype(n)>::max())
				n = std::min(n, static_cast<decltype(n)>(std::numeric_limits<std::streamsize>::max()));
			assert(n > 0);
			output.write(reinterpret_cast<const char*>(pixels), static_cast<std::streamsize>(n));


			crc32(pixels, n);
			adler32(pixels, n);


			count -= n;
			pixels += n;
			positionX += n;
			uncompRemain -= n;
			deflateFilled += n;
		}

		if (deflateFilled >= DEFLATE_MAX_BLOCK_SIZE)
			deflateFilled = 0;

		if (positionX == lineSize) {
			positionX = 0;
			positionY++;
			if (positionY == height) {
				uint8_t footer[] = {
					0, 0, 0, 0,
					0, 0, 0, 0,

					0x00, 0x00, 0x00, 0x00,
					0x49, 0x45, 0x4E, 0x44,
					0xAE, 0x42, 0x60, 0x82,
				};
				putBigUint32(adler, &footer[0]);
				crc32(&footer[0], 4);
				putBigUint32(crc, &footer[4]);
				write(footer);
			}
		}
	}
}


void TinyPngOut::crc32(const uint8_t data[], size_t len) {
	crc = ~crc;
	for (size_t i = 0; i < len; i++) {
		for (int j = 0; j < 8; j++) {
			uint32_t bit = (crc ^ (data[i] >> j)) & 1;
			crc = (crc >> 1) ^ ((-bit) & UINT32_C(0xEDB88320));
		}
	}
	crc = ~crc;
}


void TinyPngOut::adler32(const uint8_t data[], size_t len) {
	uint32_t s1 = adler & 0xFFFF;
	uint32_t s2 = adler >> 16;
	for (size_t i = 0; i < len; i++) {
		s1 = (s1 + data[i]) % 65521;
		s2 = (s2 + s1) % 65521;
	}
	adler = s2 << 16 | s1;
}


void TinyPngOut::putBigUint32(uint32_t val, uint8_t array[4]) {
	for (int i = 0; i < 4; i++)
		array[i] = static_cast<uint8_t>(val >> ((3 - i) * 8));
}

