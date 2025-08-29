#include "common/Message.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <cstring>
#include <arpa/inet.h>


static void ok(const char* what, bool cond) {
    std::cout << (cond ? "[OK] " : "[FAIL] ") << what << std::endl;
    if (!cond) std::abort();
}

// simuliraj TCP: bajtovi stižu u komadićima, sklapamo frame sa 4-bajtnim length prefiksom
static bool try_extract_one_framed_message(std::vector<uint8_t>& inbox, std::vector<uint8_t>& one_frame) {
    if (inbox.size() < sizeof(uint32_t)) return false;
    uint32_t netlen;
    std::memcpy(&netlen, inbox.data(), sizeof(uint32_t));
    uint32_t total_len = ntohl(netlen);
    if (inbox.size() < sizeof(uint32_t) + total_len) return false;

    one_frame.assign(inbox.begin(), inbox.begin() + sizeof(uint32_t) + total_len);
    inbox.erase(inbox.begin(), inbox.begin() + sizeof(uint32_t) + total_len);
    return true;
}

int main() {
    using namespace transport;

    // 1) Kreiraj poruku i napuni par polja
    auto m = MessageFactory::createConnectRequest("client_X");
    m->addInt("num", 42);
    m->addBool("flag", true);
    m->calculateChecksum();

    // 2) RAW serialize/deserialize
    auto raw = m->serialize();
    Message m2;
    bool ok_deser = m2.deserialize(raw);
    ok("deserialize(raw)", ok_deser);
    ok("fields roundtrip (string)", m2.getString("client_id") == "client_X");
    ok("fields roundtrip (int)",    m2.getInt("num") == 42);
    ok("fields roundtrip (bool)",   m2.getBool("flag") == true);
    ok("checksum valid",            m2.verifyChecksum());

    // 3) Korupcija jednog bajta -> checksum treba pasti
    auto corrupted = raw;
    if (!corrupted.empty()) corrupted.back() ^= 0xFF;
    Message m3;
    bool deser_corrupted = m3.deserialize(corrupted);
    // deserialize može i proći (format je još uvijek konzistentan), ali checksum NE SMIJE proći
    ok("deserialize(corrupted) format ok", deser_corrupted);
    ok("checksum fails on corrupted", !m3.verifyChecksum());

    // 4) Stream framing (serializeStream/deserializeStream)
    auto framed = m->serializeStream();

    // simuliraj TCP “chunking”: pošalji u 3 mala komada u “inbox”
    std::vector<uint8_t> inbox;
    size_t cut1 = 3;                                // prva 3 bajta
    size_t cut2 = std::min<size_t>(12, framed.size()); // zatim još malo
    inbox.insert(inbox.end(), framed.begin(), framed.begin() + cut1);
    std::vector<uint8_t> out;
    ok("no full frame yet", !try_extract_one_framed_message(inbox, out));

    inbox.insert(inbox.end(), framed.begin() + cut1, framed.begin() + cut2);
    ok("still no full frame", !try_extract_one_framed_message(inbox, out));

    inbox.insert(inbox.end(), framed.begin() + cut2, framed.end());
    ok("now full frame", try_extract_one_framed_message(inbox, out));
    ok("inbox empty afterwards", inbox.empty());

    // sad imamo jedan kompletan frame -> probaj stream deserializaciju
    Message ms;
    bool ok_stream_deser = ms.deserializeStream(out);
    ok("deserializeStream(frame)", ok_stream_deser);
    ok("stream fields roundtrip", ms.getString("client_id") == "client_X");

    // 5) Testiraj binary polje (addBinary/getBinary)
    std::vector<uint8_t> blob = {1,2,3,4,5,250,251,252};
    m->addBinary("bin", blob);
    m->calculateChecksum();
    auto raw2 = m->serialize();
    Message mb;
    ok("deserialize(raw with bin)", mb.deserialize(raw2));
    auto blob_out = mb.getBinary("bin");
    ok("binary size matches", blob_out.size() == blob.size());
    bool same = (blob_out == blob);
    ok("binary content matches", same);

    // 6) Više poruka u jednom streamu (back-to-back frames)
    auto mA = MessageFactory::createConnectRequest("A");
    mA->calculateChecksum();
    auto mB = MessageFactory::createConnectRequest("B");
    mB->calculateChecksum();
    auto fA = mA->serializeStream();
    auto fB = mB->serializeStream();

    std::vector<uint8_t> inbox2;
    inbox2.insert(inbox2.end(), fA.begin(), fA.end());
    inbox2.insert(inbox2.end(), fB.begin(), fB.end());

    // izvuci A
    std::vector<uint8_t> frameA;
    ok("extract frame A", try_extract_one_framed_message(inbox2, frameA));
    Message outA; ok("deser A", outA.deserializeStream(frameA));
    ok("A == 'A'", outA.getString("client_id") == "A");

    // izvuci B
    std::vector<uint8_t> frameB;
    ok("extract frame B", try_extract_one_framed_message(inbox2, frameB));
    Message outB; ok("deser B", outB.deserializeStream(frameB));
    ok("B == 'B'", outB.getString("client_id") == "B");

    // 7) Negativni test: stream header kaže da je frame duži nego što imamo
    std::vector<uint8_t> half = fA;
    if (!half.empty()) half.pop_back(); // “odreži” jedan bajt
    Message bad;
    bool ok_bad = bad.deserializeStream(half); // treba biti false
    ok("deserializeStream(incomplete) fails", !ok_bad);

    std::cout << "All stream/byte tests passed.\n";
    return 0;
}

