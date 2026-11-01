




















#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>





class TinyPngOut final {




	private: std::uint32_t width;
	private: std::uint32_t height;
	private: std::uint32_t lineSize;


	private: std::ostream &output;
	private: std::uint32_t positionX;
	private: std::uint32_t positionY;
	private: std::uint32_t uncompRemain;
	private: std::uint16_t deflateFilled;
	private: std::uint32_t crc;
	private: std::uint32_t adler;










	public: explicit TinyPngOut(std::uint32_t w, std::uint32_t h, std::ostream &out);










	public: void write(const std::uint8_t pixels[], size_t count);






	private: void crc32(const std::uint8_t data[], size_t len);



	private: void adler32(const std::uint8_t data[], size_t len);





	private: template <std::size_t N>
	void write(const std::uint8_t (&data)[N]) {
		output.write(reinterpret_cast<const char*>(data), sizeof(data));
	}


	private: static void putBigUint32(std::uint32_t val, std::uint8_t array[4]);


	private: static constexpr std::uint16_t DEFLATE_MAX_BLOCK_SIZE = 65535;

};
