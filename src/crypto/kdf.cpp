#include "icpu/crypto/kdf.hpp"

#include <monocypher.h>

#include <memory>
#include <new>

#include "icpu/core/error.hpp"

namespace icpu::crypto {

SessionKeys derive_session_keys(std::string_view password, const Digest256& salt, const KdfParams& params) {
    require(!password.empty(), ErrorCode::InvalidArgument, "La contrasena no puede estar vacia");
    require(params.memory_kib >= 8 * params.lanes && params.passes >= 1, ErrorCode::InvalidArgument,
            "Parametros de Argon2 invalidos");

    // Area de trabajo de Argon2: memory_kib bloques de 1 KiB.
    const std::size_t work_size = static_cast<std::size_t>(params.memory_kib) * 1024;
    std::unique_ptr<std::uint8_t[]> work(new (std::nothrow) std::uint8_t[work_size]);
    require(work != nullptr, ErrorCode::Internal, "Memoria insuficiente para Argon2id");

    crypto_argon2_config config{};
    config.algorithm = CRYPTO_ARGON2_ID;
    config.nb_blocks = params.memory_kib;
    config.nb_passes = params.passes;
    config.nb_lanes = params.lanes;

    crypto_argon2_inputs inputs{};
    inputs.pass = reinterpret_cast<const std::uint8_t*>(password.data());
    inputs.pass_size = static_cast<std::uint32_t>(password.size());
    inputs.salt = salt.data();
    inputs.salt_size = static_cast<std::uint32_t>(salt.size());

    // Separacion de dominio adicional: el "ad" de Argon2 identifica la aplicacion y version.
    static constexpr char kContext[] = "icpu/v1/argon2id";
    crypto_argon2_extras extras{};
    extras.ad = reinterpret_cast<const std::uint8_t*>(kContext);
    extras.ad_size = sizeof(kContext) - 1;

    Secret<64> master;
    crypto_argon2(master.data(), static_cast<std::uint32_t>(master.size()), work.get(), config, inputs, extras);
    crypto_wipe(work.get(), work_size);

    const ByteSpan m{master.data(), master.size()};
    SessionKeys keys;
    keys.encryption = derive_subkey(m, "icpu/v1/encryption");
    keys.placement = derive_subkey(m, "icpu/v1/placement");
    keys.header = derive_subkey(m, "icpu/v2/header");
    return keys;
}

}  // namespace icpu::crypto
