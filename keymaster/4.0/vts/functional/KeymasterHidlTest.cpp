/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "KeymasterHidlTest.h"

#include <chrono>
#include <vector>

#include <android-base/logging.h>
#include <android/hidl/manager/1.0/IServiceManager.h>

#include <keymasterV4_0/key_param_output.h>
#include <keymasterV4_0/keymaster_utils.h>

namespace android {
namespace hardware {
namespace keymaster {
namespace V4_0 {

::std::ostream& operator<<(::std::ostream& os, const AuthorizationSet& set) {
    if (set.size() == 0)
        os << "(Empty)" << ::std::endl;
    else {
        os << "\n";
        for (size_t i = 0; i < set.size(); ++i) os << set[i] << ::std::endl;
    }
    return os;
}

namespace test {

using namespace std::literals::chrono_literals;

void KeymasterHidlTest::InitializeKeymaster(sp<IKeymasterDevice> keymaster) {
    ASSERT_NE(keymaster, nullptr);
    keymaster_ = keymaster;
    ASSERT_TRUE(keymaster_
                        ->getHardwareInfo([&](SecurityLevel securityLevel, const hidl_string& name,
                                              const hidl_string& author) {
                            securityLevel_ = securityLevel;
                            name_ = name;
                            author_ = author;
                        })
                        .isOk());

    os_version_ = support::getOsVersion();
    os_patch_level_ = support::getOsPatchlevel();
}

void KeymasterHidlTest::SetUp() {
    InitializeKeymaster(IKeymasterDevice::getService(GetParam()));
}

ErrorCode KeymasterHidlTest::GenerateKey(const AuthorizationSet& key_desc, HidlBuf* key_blob,
                                         KeyCharacteristics* key_characteristics) {
    EXPECT_NE(key_blob, nullptr) << "Key blob pointer must not be null.  Test bug";
    EXPECT_EQ(0U, key_blob->size()) << "Key blob not empty before generating key.  Test bug.";
    EXPECT_NE(key_characteristics, nullptr)
        << "Previous characteristics not deleted before generating key.  Test bug.";

    ErrorCode error;
    EXPECT_TRUE(keymaster_
                    ->generateKey(key_desc.hidl_data(),
                                  [&](ErrorCode hidl_error, const HidlBuf& hidl_key_blob,
                                      const KeyCharacteristics& hidl_key_characteristics) {
                                      error = hidl_error;
                                      *key_blob = hidl_key_blob;
                                      *key_characteristics = hidl_key_characteristics;
                                  })
                    .isOk());
    // On error, blob & characteristics should be empty.
    if (error != ErrorCode::OK) {
        EXPECT_EQ(0U, key_blob->size());
        EXPECT_EQ(0U, (key_characteristics->softwareEnforced.size() +
                       key_characteristics->hardwareEnforced.size()));
    }
    return error;
}

ErrorCode KeymasterHidlTest::GenerateKey(const AuthorizationSet& key_desc) {
    return GenerateKey(key_desc, &key_blob_, &key_characteristics_);
}

ErrorCode KeymasterHidlTest::ImportKey(const AuthorizationSet& key_desc, KeyFormat format,
                                       const string& key_material, HidlBuf* key_blob,
                                       KeyCharacteristics* key_characteristics) {
    ErrorCode error;
    EXPECT_TRUE(keymaster_
                    ->importKey(key_desc.hidl_data(), format, HidlBuf(key_material),
                                [&](ErrorCode hidl_error, const HidlBuf& hidl_key_blob,
                                    const KeyCharacteristics& hidl_key_characteristics) {
                                    error = hidl_error;
                                    *key_blob = hidl_key_blob;
                                    *key_characteristics = hidl_key_characteristics;
                                })
                    .isOk());
    // On error, blob & characteristics should be empty.
    if (error != ErrorCode::OK) {
        EXPECT_EQ(0U, key_blob->size());
        EXPECT_EQ(0U, (key_characteristics->softwareEnforced.size() +
                       key_characteristics->hardwareEnforced.size()));
    }
    return error;
}

ErrorCode KeymasterHidlTest::ImportKey(const AuthorizationSet& key_desc, KeyFormat format,
                                       const string& key_material) {
    return ImportKey(key_desc, format, key_material, &key_blob_, &key_characteristics_);
}

ErrorCode KeymasterHidlTest::ImportWrappedKey(string wrapped_key, string wrapping_key,
                                              const AuthorizationSet& wrapping_key_desc,
                                              string masking_key,
                                              const AuthorizationSet& unwrapping_params) {
    ErrorCode error;
    EXPECT_EQ(ErrorCode::OK, ImportKey(wrapping_key_desc, KeyFormat::PKCS8, wrapping_key));
    EXPECT_TRUE(keymaster_
                    ->importWrappedKey(HidlBuf(wrapped_key), key_blob_, HidlBuf(masking_key),
                                       unwrapping_params.hidl_data(), 0 /* passwordSid */,
                                       0 /* biometricSid */,
                                       [&](ErrorCode hidl_error, const HidlBuf& hidl_key_blob,
                                           const KeyCharacteristics& hidl_key_characteristics) {
                                           error = hidl_error;
                                           key_blob_ = hidl_key_blob;
                                           key_characteristics_ = hidl_key_characteristics;
                                       })
                    .isOk());
    return error;
}

ErrorCode KeymasterHidlTest::ExportKey(KeyFormat format, const HidlBuf& key_blob,
                                       const HidlBuf& client_id, const HidlBuf& app_data,
                                       HidlBuf* key_material) {
    ErrorCode error;
    EXPECT_TRUE(keymaster_
                    ->exportKey(format, key_blob, client_id, app_data,
                                [&](ErrorCode hidl_error_code, const HidlBuf& hidl_key_material) {
                                    error = hidl_error_code;
                                    *key_material = hidl_key_material;
                                })
                    .isOk());
    // On error, blob should be empty.
    if (error != ErrorCode::OK) {
        EXPECT_EQ(0U, key_material->size());
    }
    return error;
}

ErrorCode KeymasterHidlTest::ExportKey(KeyFormat format, HidlBuf* key_material) {
    HidlBuf client_id, app_data;
    return ExportKey(format, key_blob_, client_id, app_data, key_material);
}

ErrorCode KeymasterHidlTest::DeleteKey(HidlBuf* key_blob, bool keep_key_blob) {
    auto rc = keymaster_->deleteKey(*key_blob);
    if (!keep_key_blob) *key_blob = HidlBuf();
    if (!rc.isOk()) return ErrorCode::UNKNOWN_ERROR;
    return rc;
}

ErrorCode KeymasterHidlTest::DeleteKey(bool keep_key_blob) {
    return DeleteKey(&key_blob_, keep_key_blob);
}

ErrorCode KeymasterHidlTest::DeleteAllKeys() {
    ErrorCode error = keymaster_->deleteAllKeys();
    return error;
}

void KeymasterHidlTest::CheckedDeleteKey(HidlBuf* key_blob, bool keep_key_blob) {
    auto rc = DeleteKey(key_blob, keep_key_blob);
    EXPECT_TRUE(rc == ErrorCode::OK || rc == ErrorCode::UNIMPLEMENTED);
}

void KeymasterHidlTest::CheckedDeleteKey() {
    CheckedDeleteKey(&key_blob_);
}

void KeymasterHidlTest::CheckGetCharacteristics(const HidlBuf& key_blob, const HidlBuf& client_id,
                                                const HidlBuf& app_data,
                                                KeyCharacteristics* key_characteristics) {
    HidlBuf empty_buf = {};
    EXPECT_EQ(ErrorCode::OK,
              GetCharacteristics(key_blob, client_id, app_data, key_characteristics));
    if (SecLevel() != SecurityLevel::SOFTWARE) {
        EXPECT_GT(key_characteristics->hardwareEnforced.size(), 0);
    }
    EXPECT_GT(key_characteristics->softwareEnforced.size(), 0);

    EXPECT_EQ(ErrorCode::INVALID_KEY_BLOB,
              GetCharacteristics(key_blob, empty_buf, app_data, key_characteristics));
    EXPECT_EQ(key_characteristics->hardwareEnforced.size(), 0);
    EXPECT_EQ(key_characteristics->softwareEnforced.size(), 0);

    EXPECT_EQ(ErrorCode::INVALID_KEY_BLOB,
              GetCharacteristics(key_blob, client_id, empty_buf, key_characteristics));
    EXPECT_EQ(key_characteristics->hardwareEnforced.size(), 0);
    EXPECT_EQ(key_characteristics->softwareEnforced.size(), 0);

    EXPECT_EQ(ErrorCode::INVALID_KEY_BLOB,
              GetCharacteristics(key_blob, empty_buf, empty_buf, key_characteristics));
    EXPECT_EQ(key_characteristics->hardwareEnforced.size(), 0);
    EXPECT_EQ(key_characteristics->softwareEnforced.size(), 0);
}

ErrorCode KeymasterHidlTest::GetCharacteristics(const HidlBuf& key_blob, const HidlBuf& client_id,
                                                const HidlBuf& app_data,
                                                KeyCharacteristics* key_characteristics) {
    ErrorCode error = ErrorCode::UNKNOWN_ERROR;
    EXPECT_TRUE(
        keymaster_
            ->getKeyCharacteristics(
                key_blob, client_id, app_data,
                [&](ErrorCode hidl_error, const KeyCharacteristics& hidl_key_characteristics) {
                    error = hidl_error, *key_characteristics = hidl_key_characteristics;
                })
            .isOk());
    return error;
}

ErrorCode KeymasterHidlTest::GetCharacteristics(const HidlBuf& key_blob,
                                                KeyCharacteristics* key_characteristics) {
    HidlBuf client_id, app_data;
    return GetCharacteristics(key_blob, client_id, app_data, key_characteristics);
}

ErrorCode KeymasterHidlTest::GetDebugInfo(DebugInfo* debug_info) {
    EXPECT_TRUE(keymaster_->getDebugInfo([&](const DebugInfo& hidl_debug_info) {
      *debug_info = hidl_debug_info;
    }).isOk());
    return ErrorCode::OK;
}

ErrorCode KeymasterHidlTest::Begin(KeyPurpose purpose, const HidlBuf& key_blob,
                                   const AuthorizationSet& in_params, AuthorizationSet* out_params,
                                   OperationHandle* op_handle) {
    SCOPED_TRACE("Begin");
    ErrorCode error;
    OperationHandle saved_handle = *op_handle;
    EXPECT_TRUE(keymaster_
                    ->begin(purpose, key_blob, in_params.hidl_data(), HardwareAuthToken(),
                            [&](ErrorCode hidl_error, const hidl_vec<KeyParameter>& hidl_out_params,
                                uint64_t hidl_op_handle) {
                                error = hidl_error;
                                *out_params = hidl_out_params;
                                *op_handle = hidl_op_handle;
                            })
                    .isOk());
    if (error != ErrorCode::OK) {
        // Some implementations may modify *op_handle on error.
        *op_handle = saved_handle;
    }
    return error;
}

ErrorCode KeymasterHidlTest::Begin(KeyPurpose purpose, const AuthorizationSet& in_params,
                                   AuthorizationSet* out_params) {
    SCOPED_TRACE("Begin");
    EXPECT_EQ(kOpHandleSentinel, op_handle_);
    return Begin(purpose, key_blob_, in_params, out_params, &op_handle_);
}

ErrorCode KeymasterHidlTest::Begin(KeyPurpose purpose, const AuthorizationSet& in_params) {
    SCOPED_TRACE("Begin");
    AuthorizationSet out_params;
    ErrorCode error = Begin(purpose, in_params, &out_params);
    EXPECT_TRUE(out_params.empty());
    return error;
}

ErrorCode KeymasterHidlTest::Update(OperationHandle op_handle, const AuthorizationSet& in_params,
                                    const string& input, AuthorizationSet* out_params,
                                    string* output, size_t* input_consumed) {
    SCOPED_TRACE("Update");
    ErrorCode error;
    EXPECT_TRUE(keymaster_
                    ->update(op_handle, in_params.hidl_data(), HidlBuf(input), HardwareAuthToken(),
                             VerificationToken(),
                             [&](ErrorCode hidl_error, uint32_t hidl_input_consumed,
                                 const hidl_vec<KeyParameter>& hidl_out_params,
                                 const HidlBuf& hidl_output) {
                                 error = hidl_error;
                                 out_params->push_back(AuthorizationSet(hidl_out_params));
                                 output->append(hidl_output.to_string());
                                 *input_consumed = hidl_input_consumed;
                             })
                    .isOk());
    return error;
}

ErrorCode KeymasterHidlTest::Update(const string& input, string* out, size_t* input_consumed) {
    SCOPED_TRACE("Update");
    AuthorizationSet out_params;
    ErrorCode error = Update(op_handle_, AuthorizationSet() /* in_params */, input, &out_params,
                             out, input_consumed);
    EXPECT_TRUE(out_params.empty());
    return error;
}

ErrorCode KeymasterHidlTest::Finish(OperationHandle op_handle, const AuthorizationSet& in_params,
                                    const string& input, const string& signature,
                                    AuthorizationSet* out_params, string* output) {
    SCOPED_TRACE("Finish");
    ErrorCode error;
    EXPECT_TRUE(
        keymaster_
            ->finish(op_handle, in_params.hidl_data(), HidlBuf(input), HidlBuf(signature),
                     HardwareAuthToken(), VerificationToken(),
                     [&](ErrorCode hidl_error, const hidl_vec<KeyParameter>& hidl_out_params,
                         const HidlBuf& hidl_output) {
                         error = hidl_error;
                         *out_params = hidl_out_params;
                         output->append(hidl_output.to_string());
                     })
            .isOk());
    op_handle_ = kOpHandleSentinel;  // So dtor doesn't Abort().
    return error;
}

ErrorCode KeymasterHidlTest::Finish(const string& message, string* output) {
    SCOPED_TRACE("Finish");
    AuthorizationSet out_params;
    string finish_output;
    ErrorCode error = Finish(op_handle_, AuthorizationSet() /* in_params */, message,
                             "" /* signature */, &out_params, output);
    if (error != ErrorCode::OK) {
        return error;
    }
    EXPECT_EQ(0U, out_params.size());
    return error;
}

ErrorCode KeymasterHidlTest::Finish(const string& message, const string& signature,
                                    string* output) {
    SCOPED_TRACE("Finish");
    AuthorizationSet out_params;
    ErrorCode error = Finish(op_handle_, AuthorizationSet() /* in_params */, message, signature,
                             &out_params, output);
    op_handle_ = kOpHandleSentinel;  // So dtor doesn't Abort().
    if (error != ErrorCode::OK) {
        return error;
    }
    EXPECT_EQ(0U, out_params.size());
    return error;
}

ErrorCode KeymasterHidlTest::Abort(OperationHandle op_handle) {
    SCOPED_TRACE("Abort");
    auto retval = keymaster_->abort(op_handle);
    EXPECT_TRUE(retval.isOk());
    return retval;
}

void KeymasterHidlTest::AbortIfNeeded() {
    SCOPED_TRACE("AbortIfNeeded");
    if (op_handle_ != kOpHandleSentinel) {
        EXPECT_EQ(ErrorCode::OK, Abort(op_handle_));
        op_handle_ = kOpHandleSentinel;
    }
}

ErrorCode KeymasterHidlTest::AttestKey(const HidlBuf& key_blob,
                                       const AuthorizationSet& attest_params,
                                       hidl_vec<hidl_vec<uint8_t>>* cert_chain) {
    SCOPED_TRACE("AttestKey");
    ErrorCode error;
    auto rc = keymaster_->attestKey(
        key_blob, attest_params.hidl_data(),
        [&](ErrorCode hidl_error, const hidl_vec<hidl_vec<uint8_t>>& hidl_cert_chain) {
            error = hidl_error;
            *cert_chain = hidl_cert_chain;
        });

    EXPECT_TRUE(rc.isOk()) << rc.description();
    if (!rc.isOk()) return ErrorCode::UNKNOWN_ERROR;

    return error;
}

ErrorCode KeymasterHidlTest::AttestKey(const AuthorizationSet& attest_params,
                                       hidl_vec<hidl_vec<uint8_t>>* cert_chain) {
    SCOPED_TRACE("AttestKey");
    return AttestKey(key_blob_, attest_params, cert_chain);
}

string KeymasterHidlTest::ProcessMessage(const HidlBuf& key_blob, KeyPurpose operation,
                                         const string& message, const AuthorizationSet& in_params,
                                         AuthorizationSet* out_params) {
    SCOPED_TRACE("ProcessMessage");
    AuthorizationSet begin_out_params;
    EXPECT_EQ(ErrorCode::OK, Begin(operation, key_blob, in_params, &begin_out_params, &op_handle_));

    string output;
    size_t consumed = 0;
    AuthorizationSet update_params;
    AuthorizationSet update_out_params;
    EXPECT_EQ(ErrorCode::OK,
              Update(op_handle_, update_params, message, &update_out_params, &output, &consumed));

    string unused;
    AuthorizationSet finish_params;
    AuthorizationSet finish_out_params;
    EXPECT_EQ(ErrorCode::OK, Finish(op_handle_, finish_params, message.substr(consumed), unused,
                                    &finish_out_params, &output));
    op_handle_ = kOpHandleSentinel;

    out_params->push_back(begin_out_params);
    out_params->push_back(finish_out_params);
    return output;
}

string KeymasterHidlTest::SignMessage(const HidlBuf& key_blob, const string& message,
                                      const AuthorizationSet& params) {
    SCOPED_TRACE("SignMessage");
    AuthorizationSet out_params;
    string signature = ProcessMessage(key_blob, KeyPurpose::SIGN, message, params, &out_params);
    EXPECT_TRUE(out_params.empty());
    return signature;
}

string KeymasterHidlTest::SignMessage(const string& message, const AuthorizationSet& params) {
    SCOPED_TRACE("SignMessage");
    return SignMessage(key_blob_, message, params);
}

string KeymasterHidlTest::MacMessage(const string& message, Digest digest, size_t mac_length) {
    SCOPED_TRACE("MacMessage");
    return SignMessage(
        key_blob_, message,
        AuthorizationSetBuilder().Digest(digest).Authorization(TAG_MAC_LENGTH, mac_length));
}

void KeymasterHidlTest::CheckAesIncrementalEncryptOperation(BlockMode block_mode,
                                                            int message_size) {
    auto builder = AuthorizationSetBuilder()
                           .Authorization(TAG_NO_AUTH_REQUIRED)
                           .AesEncryptionKey(128)
                           .BlockMode(block_mode)
                           .Padding(PaddingMode::NONE);
    if (block_mode == BlockMode::GCM) {
        builder.Authorization(TAG_MIN_MAC_LENGTH, 128);
    }
    ASSERT_EQ(ErrorCode::OK, GenerateKey(builder));

    for (int increment = 1; increment <= message_size; ++increment) {
        string message(message_size, 'a');
        auto params = AuthorizationSetBuilder()
                              .BlockMode(block_mode)
                              .Padding(PaddingMode::NONE)
                              .Authorization(TAG_MAC_LENGTH, 128) /* for GCM */;

        AuthorizationSet output_params;
        EXPECT_EQ(ErrorCode::OK, Begin(KeyPurpose::ENCRYPT, params, &output_params));

        string ciphertext;
        size_t input_consumed;
        string to_send;
        for (size_t i = 0; i < message.size(); i += increment) {
            to_send.append(message.substr(i, increment));
            EXPECT_EQ(ErrorCode::OK, Update(to_send, &ciphertext, &input_consumed));
            EXPECT_EQ(to_send.length(), input_consumed);
            to_send = to_send.substr(input_consumed);
            EXPECT_EQ(0U, to_send.length());

            switch (block_mode) {
                case BlockMode::ECB:
                case BlockMode::CBC:
                    // Implementations must take as many blocks as possible, leaving less than
                    // a block.
                    EXPECT_LE(to_send.length(), 16U);
                    break;
                case BlockMode::GCM:
                case BlockMode::CTR:
                    // Implementations must always take all the data.
                    EXPECT_EQ(0U, to_send.length());
                    break;
            }
        }
        EXPECT_EQ(ErrorCode::OK, Finish(to_send, &ciphertext)) << "Error sending " << to_send;

        switch (block_mode) {
            case BlockMode::GCM:
                EXPECT_EQ(message.size() + 16, ciphertext.size());
                break;
            case BlockMode::CTR:
                EXPECT_EQ(message.size(), ciphertext.size());
                break;
            case BlockMode::CBC:
            case BlockMode::ECB:
                EXPECT_EQ(message.size() + message.size() % 16, ciphertext.size());
                break;
        }

        auto iv = output_params.GetTagValue(TAG_NONCE);
        switch (block_mode) {
            case BlockMode::CBC:
            case BlockMode::GCM:
            case BlockMode::CTR:
                ASSERT_TRUE(iv.isOk()) << "No IV for block mode " << block_mode;
                EXPECT_EQ(block_mode == BlockMode::GCM ? 12U : 16U, iv.value().size());
                params.push_back(TAG_NONCE, iv.value());
                break;

            case BlockMode::ECB:
                EXPECT_FALSE(iv.isOk()) << "ECB mode should not generate IV";
                break;
        }

        EXPECT_EQ(ErrorCode::OK, Begin(KeyPurpose::DECRYPT, params))
                << "Decrypt begin() failed for block mode " << block_mode;

        string plaintext;
        for (size_t i = 0; i < ciphertext.size(); i += increment) {
            to_send.append(ciphertext.substr(i, increment));
            EXPECT_EQ(ErrorCode::OK, Update(to_send, &plaintext, &input_consumed));
            to_send = to_send.substr(input_consumed);
        }
        ErrorCode error = Finish(to_send, &plaintext);
        ASSERT_EQ(ErrorCode::OK, error) << "Decryption failed for block mode " << block_mode
                                        << " and increment " << increment;
        if (error == ErrorCode::OK) {
            ASSERT_EQ(message, plaintext) << "Decryption didn't match for block mode " << block_mode
                                          << " and increment " << increment;
        }
    }
}

void KeymasterHidlTest::CheckHmacTestVector(const string& key, const string& message, Digest digest,
                                            const string& expected_mac) {
    SCOPED_TRACE("CheckHmacTestVector");
    ASSERT_EQ(ErrorCode::OK,
              ImportKey(AuthorizationSetBuilder()
                            .Authorization(TAG_NO_AUTH_REQUIRED)
                            .HmacKey(key.size() * 8)
                            .Authorization(TAG_MIN_MAC_LENGTH, expected_mac.size() * 8)
                            .Digest(digest),
                        KeyFormat::RAW, key));
    string signature = MacMessage(message, digest, expected_mac.size() * 8);
    EXPECT_EQ(expected_mac, signature)
        << "Test vector didn't match for key of size " << key.size() << " message of size "
        << message.size() << " and digest " << digest;
    CheckedDeleteKey();
}

void KeymasterHidlTest::CheckAesCtrTestVector(const string& key, const string& nonce,
                                              const string& message,
                                              const string& expected_ciphertext) {
    SCOPED_TRACE("CheckAesCtrTestVector");
    ASSERT_EQ(ErrorCode::OK, ImportKey(AuthorizationSetBuilder()
                                           .Authorization(TAG_NO_AUTH_REQUIRED)
                                           .AesEncryptionKey(key.size() * 8)
                                           .BlockMode(BlockMode::CTR)
                                           .Authorization(TAG_CALLER_NONCE)
                                           .Padding(PaddingMode::NONE),
                                       KeyFormat::RAW, key));

    auto params = AuthorizationSetBuilder()
                      .Authorization(TAG_NONCE, nonce.data(), nonce.size())
                      .BlockMode(BlockMode::CTR)
                      .Padding(PaddingMode::NONE);
    AuthorizationSet out_params;
    string ciphertext = EncryptMessage(key_blob_, message, params, &out_params);
    EXPECT_EQ(expected_ciphertext, ciphertext);
}

void KeymasterHidlTest::CheckTripleDesTestVector(KeyPurpose purpose, BlockMode block_mode,
                                                 PaddingMode padding_mode, const string& key,
                                                 const string& iv, const string& input,
                                                 const string& expected_output) {
    auto authset = AuthorizationSetBuilder()
                       .TripleDesEncryptionKey(key.size() * 7)
                       .BlockMode(block_mode)
                       .Authorization(TAG_NO_AUTH_REQUIRED)
                       .Padding(padding_mode);
    if (iv.size()) authset.Authorization(TAG_CALLER_NONCE);
    ASSERT_EQ(ErrorCode::OK, ImportKey(authset, KeyFormat::RAW, key));
    auto begin_params = AuthorizationSetBuilder().BlockMode(block_mode).Padding(padding_mode);
    if (iv.size()) begin_params.Authorization(TAG_NONCE, iv.data(), iv.size());
    AuthorizationSet output_params;
    string output = ProcessMessage(key_blob_, purpose, input, begin_params, &output_params);
    EXPECT_EQ(expected_output, output);
}

void KeymasterHidlTest::VerifyMessage(const HidlBuf& key_blob, const string& message,
                                      const string& signature, const AuthorizationSet& params) {
    SCOPED_TRACE("VerifyMessage");
    AuthorizationSet begin_out_params;
    ASSERT_EQ(ErrorCode::OK,
              Begin(KeyPurpose::VERIFY, key_blob, params, &begin_out_params, &op_handle_));

    string output;
    AuthorizationSet update_params;
    AuthorizationSet update_out_params;
    size_t consumed;
    ASSERT_EQ(ErrorCode::OK,
              Update(op_handle_, update_params, message, &update_out_params, &output, &consumed));
    EXPECT_TRUE(output.empty());
    EXPECT_GT(consumed, 0U);

    string unused;
    AuthorizationSet finish_params;
    AuthorizationSet finish_out_params;
    EXPECT_EQ(ErrorCode::OK, Finish(op_handle_, finish_params, message.substr(consumed), signature,
                                    &finish_out_params, &output));
    op_handle_ = kOpHandleSentinel;
    EXPECT_TRUE(output.empty());
}

void KeymasterHidlTest::VerifyMessage(const string& message, const string& signature,
                                      const AuthorizationSet& params) {
    SCOPED_TRACE("VerifyMessage");
    VerifyMessage(key_blob_, message, signature, params);
}

string KeymasterHidlTest::EncryptMessage(const HidlBuf& key_blob, const string& message,
                                         const AuthorizationSet& in_params,
                                         AuthorizationSet* out_params) {
    SCOPED_TRACE("EncryptMessage");
    return ProcessMessage(key_blob, KeyPurpose::ENCRYPT, message, in_params, out_params);
}

string KeymasterHidlTest::EncryptMessage(const string& message, const AuthorizationSet& params,
                                         AuthorizationSet* out_params) {
    SCOPED_TRACE("EncryptMessage");
    return EncryptMessage(key_blob_, message, params, out_params);
}

string KeymasterHidlTest::EncryptMessage(const string& message, const AuthorizationSet& params) {
    SCOPED_TRACE("EncryptMessage");
    AuthorizationSet out_params;
    string ciphertext = EncryptMessage(message, params, &out_params);
    EXPECT_TRUE(out_params.empty()) << "Output params should be empty. Contained: " << out_params;
    return ciphertext;
}

string KeymasterHidlTest::EncryptMessage(const string& message, BlockMode block_mode,
                                         PaddingMode padding) {
    SCOPED_TRACE("EncryptMessage");
    auto params = AuthorizationSetBuilder().BlockMode(block_mode).Padding(padding);
    AuthorizationSet out_params;
    string ciphertext = EncryptMessage(message, params, &out_params);
    EXPECT_TRUE(out_params.empty()) << "Output params should be empty. Contained: " << out_params;
    return ciphertext;
}

string KeymasterHidlTest::EncryptMessage(const string& message, BlockMode block_mode,
                                         PaddingMode padding, HidlBuf* iv_out) {
    SCOPED_TRACE("EncryptMessage");
    auto params = AuthorizationSetBuilder().BlockMode(block_mode).Padding(padding);
    AuthorizationSet out_params;
    string ciphertext = EncryptMessage(message, params, &out_params);
    EXPECT_EQ(1U, out_params.size());
    auto ivVal = out_params.GetTagValue(TAG_NONCE);
    EXPECT_TRUE(ivVal.isOk());
    if (ivVal.isOk()) *iv_out = ivVal.value();
    return ciphertext;
}

string KeymasterHidlTest::EncryptMessage(const string& message, BlockMode block_mode,
                                         PaddingMode padding, const HidlBuf& iv_in) {
    SCOPED_TRACE("EncryptMessage");
    auto params = AuthorizationSetBuilder()
                      .BlockMode(block_mode)
                      .Padding(padding)
                      .Authorization(TAG_NONCE, iv_in);
    AuthorizationSet out_params;
    string ciphertext = EncryptMessage(message, params, &out_params);
    return ciphertext;
}

string KeymasterHidlTest::EncryptMessage(const string& message, BlockMode block_mode,
                                         PaddingMode padding, uint8_t mac_length_bits,
                                         const HidlBuf& iv_in) {
    SCOPED_TRACE("EncryptMessage");
    auto params = AuthorizationSetBuilder()
                          .BlockMode(block_mode)
                          .Padding(padding)
                          .Authorization(TAG_MAC_LENGTH, mac_length_bits)
                          .Authorization(TAG_NONCE, iv_in);
    AuthorizationSet out_params;
    string ciphertext = EncryptMessage(message, params, &out_params);
    return ciphertext;
}

string KeymasterHidlTest::DecryptMessage(const HidlBuf& key_blob, const string& ciphertext,
                                         const AuthorizationSet& params) {
    SCOPED_TRACE("DecryptMessage");
    AuthorizationSet out_params;
    string plaintext =
        ProcessMessage(key_blob, KeyPurpose::DECRYPT, ciphertext, params, &out_params);
    EXPECT_TRUE(out_params.empty());
    return plaintext;
}

string KeymasterHidlTest::DecryptMessage(const string& ciphertext, const AuthorizationSet& params) {
    SCOPED_TRACE("DecryptMessage");
    return DecryptMessage(key_blob_, ciphertext, params);
}

string KeymasterHidlTest::DecryptMessage(const string& ciphertext, BlockMode block_mode,
                                         PaddingMode padding_mode, const HidlBuf& iv) {
    SCOPED_TRACE("DecryptMessage");
    auto params = AuthorizationSetBuilder()
                      .BlockMode(block_mode)
                      .Padding(padding_mode)
                      .Authorization(TAG_NONCE, iv);
    return DecryptMessage(key_blob_, ciphertext, params);
}

std::pair<ErrorCode, HidlBuf> KeymasterHidlTest::UpgradeKey(const HidlBuf& key_blob) {
    std::pair<ErrorCode, HidlBuf> retval;
    keymaster_->upgradeKey(key_blob, hidl_vec<KeyParameter>(),
                           [&](ErrorCode error, const hidl_vec<uint8_t>& upgraded_blob) {
                               retval = std::tie(error, upgraded_blob);
                           });
    return retval;
}
std::vector<uint32_t> KeymasterHidlTest::ValidKeySizes(Algorithm algorithm) {
    switch (algorithm) {
        case Algorithm::RSA:
            switch (SecLevel()) {
                case SecurityLevel::SOFTWARE:
                case SecurityLevel::TRUSTED_ENVIRONMENT:
                    return {2048, 3072, 4096};
                case SecurityLevel::STRONGBOX:
                    return {2048};
                default:
                    ADD_FAILURE() << "Invalid security level " << uint32_t(SecLevel());
                    break;
            }
            break;
        case Algorithm::EC:
            switch (SecLevel()) {
                case SecurityLevel::SOFTWARE:
                case SecurityLevel::TRUSTED_ENVIRONMENT:
                    return {224, 256, 384, 521};
                case SecurityLevel::STRONGBOX:
                    return {256};
                default:
                    ADD_FAILURE() << "Invalid security level " << uint32_t(SecLevel());
                    break;
            }
            break;
        case Algorithm::AES:
            return {128, 256};
        case Algorithm::TRIPLE_DES:
            return {168};
        case Algorithm::HMAC: {
            std::vector<uint32_t> retval((512 - 64) / 8 + 1);
            uint32_t size = 64 - 8;
            std::generate(retval.begin(), retval.end(), [&]() { return (size += 8); });
            return retval;
        }
        default:
            ADD_FAILURE() << "Invalid Algorithm: " << algorithm;
            return {};
    }
    ADD_FAILURE() << "Should be impossible to get here";
    return {};
}

std::vector<uint32_t> KeymasterHidlTest::InvalidKeySizes(Algorithm algorithm) {
    if (SecLevel() == SecurityLevel::STRONGBOX) {
        switch (algorithm) {
            case Algorithm::RSA:
                return {3072, 4096};
            case Algorithm::EC:
                return {224, 384, 521};
            case Algorithm::AES:
                return {192};
            default:
                return {};
        }
    }
    return {};
}

std::vector<EcCurve> KeymasterHidlTest::ValidCurves() {
    if (securityLevel_ == SecurityLevel::STRONGBOX) {
        return {EcCurve::P_256};
    } else {
        return {EcCurve::P_224, EcCurve::P_256, EcCurve::P_384, EcCurve::P_521};
    }
}

std::vector<EcCurve> KeymasterHidlTest::InvalidCurves() {
    if (SecLevel() == SecurityLevel::TRUSTED_ENVIRONMENT) return {};
    CHECK(SecLevel() == SecurityLevel::STRONGBOX);
    return {EcCurve::P_224, EcCurve::P_384, EcCurve::P_521};
}

std::vector<Digest> KeymasterHidlTest::ValidDigests(bool withNone, bool withMD5) {
    switch (SecLevel()) {
        case SecurityLevel::SOFTWARE:
        case SecurityLevel::TRUSTED_ENVIRONMENT:
            if (withNone) {
                if (withMD5)
                    return {Digest::NONE,      Digest::MD5,       Digest::SHA1,
                            Digest::SHA_2_224, Digest::SHA_2_256, Digest::SHA_2_384,
                            Digest::SHA_2_512};
                else
                    return {Digest::NONE,      Digest::SHA1,      Digest::SHA_2_224,
                            Digest::SHA_2_256, Digest::SHA_2_384, Digest::SHA_2_512};
            } else {
                if (withMD5)
                    return {Digest::MD5,       Digest::SHA1,      Digest::SHA_2_224,
                            Digest::SHA_2_256, Digest::SHA_2_384, Digest::SHA_2_512};
                else
                    return {Digest::SHA1, Digest::SHA_2_224, Digest::SHA_2_256, Digest::SHA_2_384,
                            Digest::SHA_2_512};
            }
            break;
        case SecurityLevel::STRONGBOX:
            if (withNone)
                return {Digest::NONE, Digest::SHA_2_256};
            else
                return {Digest::SHA_2_256};
            break;
        default:
            ADD_FAILURE() << "Invalid security level " << uint32_t(SecLevel());
            break;
    }
    ADD_FAILURE() << "Should be impossible to get here";
    return {};
}

std::vector<Digest> KeymasterHidlTest::InvalidDigests() {
    return {};
}

X509* parse_cert_blob(const hidl_vec<uint8_t>& blob) {
    const uint8_t* p = blob.data();
    return d2i_X509(nullptr, &p, blob.size());
}

ASN1_OCTET_STRING* get_attestation_record(X509* certificate) {
    ASN1_OBJECT_Ptr oid(OBJ_txt2obj(kAttestionRecordOid, 1 /* dotted string format */));
    EXPECT_TRUE(!!oid.get());
    if (!oid.get()) return nullptr;

    int location = X509_get_ext_by_OBJ(certificate, oid.get(), -1 /* search from beginning */);
    EXPECT_NE(-1, location) << "Attestation extension not found in certificate";
    if (location == -1) return nullptr;

    X509_EXTENSION* attest_rec_ext = X509_get_ext(certificate, location);
    EXPECT_TRUE(!!attest_rec_ext)
            << "Found attestation extension but couldn't retrieve it?  Probably a BoringSSL bug.";
    if (!attest_rec_ext) return nullptr;

    ASN1_OCTET_STRING* attest_rec = X509_EXTENSION_get_data(attest_rec_ext);
    EXPECT_TRUE(!!attest_rec) << "Attestation extension contained no data";
    return attest_rec;
}

}  // namespace test
}  // namespace V4_0
}  // namespace keymaster
}  // namespace hardware
}  // namespace android
