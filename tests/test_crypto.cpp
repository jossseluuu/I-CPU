// crypto: BLAKE2b (vectores calculados con hashlib de Python), AEAD, KDF,
// flujo de claves y aleatorio del sistema.

#include <doctest.h>

#include <algorithm>
#include <set>

#include "helpers.hpp"
#include "icpu/crypto/aead.hpp"
#include "icpu/crypto/hash.hpp"
#include "icpu/crypto/kdf.hpp"
#include "icpu/crypto/keystream.hpp"
#include "icpu/crypto/random.hpp"

using namespace icpu;
using namespace icpu::crypto;

namespace {

std::string hex(const Digest256& d) { return to_hex(ByteSpan(d.data(), d.size())); }
std::string hex(const Key256& k) { return to_hex(ByteSpan(k.data(), k.size())); }

Key256 key_from(std::uint8_t seed) {
    Key256 k;
    for (std::size_t i = 0; i < k.size(); ++i) k.data()[i] = static_cast<std::uint8_t>(seed + i);
    return k;
}

bool same(const Key256& a, const Key256& b) { return std::equal(a.data(), a.data() + 32, b.data()); }

}  // namespace

TEST_SUITE("crypto") {

TEST_CASE("BLAKE2b-256: vectores de referencia") {
    CHECK(hex(blake2b_256({})) == "0e5751c026e543b2e8ab2eb06099daa1d1e5df47778f7787faab45cdf12fe3a8");
    CHECK(hex(blake2b_256(as_bytes("abc"))) == "bddd813c634239723171ef3fee98579b94964e3bb1cb3e427262c8c068d52319");

    Bytes big;
    for (int rep = 0; rep < 4; ++rep)
        for (int i = 0; i < 256; ++i) big.push_back(static_cast<std::uint8_t>(i));
    const char* expected = "f1551feeb252c7e60bb362205bd1ac2f70b145260a91d41e8c5d0a187549a5f2";
    CHECK(hex(blake2b_256(big)) == expected);

    // Incremental en trozos irregulares = de una vez
    Blake2b256 h;
    h.update(ByteSpan(big).subspan(0, 1));
    h.update(ByteSpan(big).subspan(1, 300));
    h.update(ByteSpan(big).subspan(301));
    CHECK(hex(h.finish()) == expected);
}

TEST_CASE("derive_subkey: BLAKE2b con clave y separacion de dominios") {
    Bytes master;
    for (int i = 0; i < 32; ++i) master.push_back(static_cast<std::uint8_t>(i));
    CHECK(hex(derive_subkey(master, "cifrado")) == "446a442a085d89180bfc1a4fd50cee5687b07c6cf455c05f45233ef8e113edc4");
    CHECK_FALSE(same(derive_subkey(master, "a"), derive_subkey(master, "b")));
}

TEST_CASE("XChaCha20-Poly1305: ida y vuelta y deteccion de manipulacion") {
    const Key256 key = key_from(7);
    const Nonce nonce = random_nonce();
    const Bytes ad{1, 2, 3};
    const Bytes msg = [] {
        Bytes m(1000);
        for (std::size_t i = 0; i < m.size(); ++i) m[i] = static_cast<std::uint8_t>(i * 31);
        return m;
    }();

    const Bytes sealed = seal(key, nonce, ad, msg);
    REQUIRE(sealed.size() == msg.size() + kMacSize);
    CHECK(Bytes(sealed.begin(), sealed.begin() + 16) != Bytes(msg.begin(), msg.begin() + 16));

    const auto opened = open(key, nonce, ad, sealed);
    REQUIRE(opened.has_value());
    CHECK(*opened == msg);

    Bytes tampered = sealed;
    tampered[500] ^= 1;
    CHECK_FALSE(open(key, nonce, ad, tampered).has_value());
    CHECK_FALSE(open(key, nonce, Bytes{1, 2, 4}, sealed).has_value());   // datos asociados distintos
    CHECK_FALSE(open(key_from(8), nonce, ad, sealed).has_value());       // otra clave
    Nonce other = nonce;
    other[0] ^= 0x80;
    CHECK_FALSE(open(key, other, ad, sealed).has_value());               // otro nonce

    // Mensaje vacio: solo MAC
    const Bytes empty_sealed = seal(key, nonce, {}, {});
    CHECK(empty_sealed.size() == kMacSize);
    CHECK(open(key, nonce, {}, empty_sealed).value().empty());
}

TEST_CASE("Argon2id: determinista, sensible a contrasena y sal") {
    Digest256 salt{};
    salt.fill(0x42);
    const KdfParams p = test::fast_kdf();
    const SessionKeys a = derive_session_keys("contrasena", salt, p);
    const SessionKeys b = derive_session_keys("contrasena", salt, p);
    CHECK(same(a.encryption, b.encryption));
    CHECK(same(a.placement, b.placement));
    CHECK_FALSE(same(a.encryption, a.placement));

    CHECK_FALSE(same(a.encryption, derive_session_keys("Contrasena", salt, p).encryption));
    Digest256 salt2 = salt;
    salt2[31] ^= 1;
    CHECK_FALSE(same(a.encryption, derive_session_keys("contrasena", salt2, p).encryption));
    CHECK_FALSE(same(a.encryption, derive_session_keys("contrasena", salt, {2048, 1, 1}).encryption));

    CHECK(test::error_of([&] { derive_session_keys("", salt, p); }) == ErrorCode::InvalidArgument);
    CHECK(test::error_of([&] { derive_session_keys("x", salt, {4, 1, 1}); }) == ErrorCode::InvalidArgument);
}

TEST_CASE("KeyStream: determinista y uniforme sin sesgo") {
    KeyStream s1(key_from(1)), s2(key_from(1)), s3(key_from(2));
    std::vector<std::uint64_t> a, b, c;
    for (int i = 0; i < 300; ++i) {  // cruza varios rellenos del buffer
        a.push_back(s1.next_u64());
        b.push_back(s2.next_u64());
        c.push_back(s3.next_u64());
    }
    CHECK(a == b);
    CHECK(a != c);

    KeyStream u(key_from(9));
    std::array<int, 7> counts{};
    for (int i = 0; i < 70000; ++i) {
        const auto v = u.uniform(7);
        REQUIRE(v < 7);
        ++counts[v];
    }
    for (int n : counts) CHECK(std::abs(n - 10000) < 500);  // ~5 sigma
    CHECK(u.uniform(1) == 0);
}

TEST_CASE("Aleatorio del sistema y RandomBits") {
    std::array<std::uint8_t, 32> x{}, y{};
    os_random(x.data(), x.size());
    os_random(y.data(), y.size());
    CHECK(x != y);

    RandomBits bits;
    int ones = 0;
    for (int i = 0; i < 10000; ++i) ones += bits.next_bit() ? 1 : 0;
    CHECK(ones > 4600);
    CHECK(ones < 5400);

    std::set<std::string> nonces;
    for (int i = 0; i < 100; ++i) {
        const Nonce n = random_nonce();
        nonces.insert(to_hex(ByteSpan(n.data(), n.size())));
    }
    CHECK(nonces.size() == 100);
}

}  // TEST_SUITE crypto
