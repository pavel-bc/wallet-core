// Copyright © 2017-2020 Trust Wallet.
//
// This file is part of Trust. The full Trust copyright notice, including
// terms governing use, modification, and redistribution, is contained in the
// file LICENSE at the root of the source code distribution tree.

#include "Signer.h"
#include "Asset.h"
#include "PackedTransaction.h"
#include "../proto/Common.pb.h"
#include "../HexCoding.h"

#include <TrezorCrypto/ecdsa.h>
#include <TrezorCrypto/nist256p1.h>
#include <TrezorCrypto/secp256k1.h>

using namespace TW;
using namespace TW::EOS;

Proto::SigningOutput Signer::sign(const Proto::SigningInput& input) noexcept {
    Proto::SigningOutput output;
    try {
        // create an asset object
        auto assetData = input.asset();
        auto asset = Asset(assetData.amount(), static_cast<uint8_t>(assetData.decimals()),
                           assetData.symbol());

        // create a transfer action
        auto action = TransferAction(input.currency(), input.sender(), input.recipient(), asset,
                                     input.memo());

        // create a Transaction and add the transfer action
        auto tx = Transaction(Data(input.reference_block_id().begin(), input.reference_block_id().end()),
                        input.reference_block_time());
        tx.actions.push_back(action);

        // get key type
        EOS::Type type = Type::Legacy;
        switch (input.private_key_type()) {
        case Proto::KeyType::LEGACY:
            type = Type::Legacy;
            break;

        case Proto::KeyType::MODERNK1:
            type = Type::ModernK1;
            break;

        case Proto::KeyType::MODERNR1:
            type = Type::ModernR1;
            break;
        default:
            break;
        }

        // sign the transaction with a Signer
        auto key = PrivateKey(Data(input.private_key().begin(), input.private_key().end()));
        auto chainId = Data(input.chain_id().begin(), input.chain_id().end());
        Signer(chainId).sign(key, type, tx);

        // Pack the transaction and add the json encoding to Signing outputput
        PackedTransaction ptx{tx, CompressionType::None};

        output.set_json_encoded(ptx.serialize().dump());
        return output;
    } catch (const std::exception& e) {
        output.mutable_error()->set_code(Common::Proto::Error_general);
        output.mutable_error()->set_text(e.what());
    }
    return output;
}

void Signer::sign(const PrivateKey& privateKey, Type type, Transaction& transaction) const {
    if (!transaction.isValid()) {
        throw std::invalid_argument("Invalid transaction!");
    }

    // values for Legacy and ModernK1
    TWCurve curve = TWCurveSECP256k1;
    auto canonicalChecker = isCanonical;

    //  Values for ModernR1
    if (type == Type::ModernR1) {
        curve = TWCurveNIST256p1;
        canonicalChecker = nullptr;
    }

    const Data result = privateKey.sign(hash(transaction), curve, canonicalChecker);

    transaction.signatures.push_back(Signature(result, type));
}

TW::Data Signer::hash(const Transaction& transaction) const noexcept {
    Data hashInput(chainID);
    transaction.serialize(hashInput);

    Data cfdHash(Hash::sha256Size); // default value for empty cfd
    if (transaction.contextFreeData.size()) {
        cfdHash = Hash::sha256(transaction.contextFreeData);
    }

    append(hashInput, cfdHash);
    return Hash::sha256(hashInput);
}

// canonical check for EOS
int Signer::isCanonical(uint8_t by, uint8_t sig[64]) {
    return !(sig[0] & 0x80) 
        && !(sig[0] == 0 && !(sig[1] & 0x80))
        && !(sig[32] & 0x80)
        && !(sig[32] == 0 && !(sig[33] & 0x80));
}
