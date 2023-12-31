// Copyright Epic Games, Inc. All Rights Reserved.

#include "StreamEncryptionHandlerComponent.h"

/* XOR Stream Encryptor Module Interface */
class UE_DEPRECATED(5.3, "This component is not supported for encryption.")
FXORStreamEncryptorModuleInterface : public FStreamEncryptorModuleInterface
{
public:
	virtual StreamEncryptor* CreateStreamEncryptorInstance();
};

/*
* XOR Block encryption
*/
class UE_DEPRECATED(5.3, "This component is not supported for encryption.")
XORSTREAMENCRYPTOR_API XORStreamEncryptor : public StreamEncryptor
{
public:
	/* Initialized the encryptor */
	void Initialize(TArray<uint8>* Key) override;

	/* Encrypts outgoing packets */
	void EncryptStream(uint8* Stream, uint32 BytesCount) override;

	/* Decrypts incoming packets */
	void DecryptStream(uint8* Stream, uint32 BytesCount) override;

	/* Get the default key size for this encryptor */
	uint32 GetDefaultKeySize() { return 4; }
};