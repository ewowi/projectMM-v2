// ArtnetOutModule packet-header tests. Asserts the 18-byte Art-Net OpDmx
// header (the bytes a v2 device sends on the wire) matches the spec at
// https://art-net.org.uk/structure/streaming-packets/artdmx-packet-definition/
//
// The packer is exposed static (ArtnetOutModule::pack_header) so we don't
// need to spin up a real UDP socket — same approach as PreviewModule's
// pack_frame.

#include <cstdint>
#include <cstring>

#include <doctest/doctest.h>

#include "modules/lights/ArtnetOutModule.h"

using namespace pmm;

TEST_CASE("ArtNet OpDmx header bytes match the spec for universe 0, 510 DMX") {
  uint8_t hdr[ArtnetOutModule::kHeaderBytes] = {0};
  ArtnetOutModule::pack_header(hdr, /*universe=*/0, /*dmx_len=*/510);

  // "Art-Net\0" 8-byte ID
  CHECK(hdr[0] == 'A'); CHECK(hdr[1] == 'r'); CHECK(hdr[2] == 't');
  CHECK(hdr[3] == '-'); CHECK(hdr[4] == 'N'); CHECK(hdr[5] == 'e');
  CHECK(hdr[6] == 't'); CHECK(hdr[7] == 0);

  // OpCode = OpDmx (0x5000) little-endian
  CHECK(hdr[8] == 0x00); CHECK(hdr[9] == 0x50);

  // ProtVer = 14 big-endian
  CHECK(hdr[10] == 0x00); CHECK(hdr[11] == 0x0e);

  CHECK(hdr[12] == 0);   // sequence (0 = disabled)
  CHECK(hdr[13] == 0);   // physical port
  CHECK(hdr[14] == 0);   // universe lo
  CHECK(hdr[15] == 0);   // universe hi (net+subnet)

  // length = 510 big-endian → 0x01 0xfe
  CHECK(hdr[16] == 0x01);
  CHECK(hdr[17] == 0xfe);
}

TEST_CASE("ArtNet OpDmx header — universe increments fit in the low byte") {
  uint8_t hdr[ArtnetOutModule::kHeaderBytes] = {0};
  ArtnetOutModule::pack_header(hdr, /*universe=*/96, /*dmx_len=*/510);
  // 96 = 0x60. Universe hi stays 0 (net+subnet not used in Sprint 6).
  CHECK(hdr[14] == 0x60);
  CHECK(hdr[15] == 0x00);
}

TEST_CASE("ArtNet OpDmx header — partial DMX byte count (e.g. last universe)") {
  // 64x64 panel = 4096 RGB = 12288 DMX. Last universe carries
  // 12288 - 24 * 510 = 48 bytes (16 pixels). 48 big-endian = 0x00 0x30.
  uint8_t hdr[ArtnetOutModule::kHeaderBytes] = {0};
  ArtnetOutModule::pack_header(hdr, /*universe=*/24, /*dmx_len=*/48);
  CHECK(hdr[16] == 0x00);
  CHECK(hdr[17] == 0x30);
}

TEST_CASE("ArtNet OpDmx: 64x64 frame splits into 25 universes (24 full + 1 partial)") {
  // The actual packing happens inside loop20ms, but the universe count it
  // produces is a derived constant we can compute here from the header
  // semantics: floor(12288 / 510) = 24 full + 1 partial = 25 universes.
  const uint32_t pixels = 64u * 64u;
  const uint32_t bytes  = pixels * 3u;
  const uint32_t full   = bytes / ArtnetOutModule::kDmxPerPacket;
  const uint32_t leftover = bytes - full * ArtnetOutModule::kDmxPerPacket;
  CHECK(full == 24);
  CHECK(leftover == 48);
}
