/* Copyright (C) 2005-2009, Thorvald Natvig <thorvald@natvig.com>

   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
   - Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.
   - Neither the name of the Mumble Developers nor the names of its
     contributors may be used to endorse or promote products derived from this
     software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "Server.h"
#include "Meta.h"

int add_ext(X509 * crt, int nid, char *value) {
	X509_EXTENSION *ex;
	X509V3_CTX ctx;
	X509V3_set_ctx_nodb(&ctx);
	X509V3_set_ctx(&ctx, crt, crt, NULL, NULL, 0);
	ex = X509V3_EXT_conf_nid(NULL, &ctx, nid, value);
	if (!ex)
		return 0;

	X509_add_ext(crt, ex, -1);
	X509_EXTENSION_free(ex);
	return 1;
}

void Server::initializeCert() {
	QByteArray crt, key, pass;

	if (! QSslSocket::supportsSsl()) {
		qFatal("Qt without SSL Support");
	}

	crt = getConf("certificate", QString()).toByteArray();
	key = getConf("key", QString()).toByteArray();
	pass = getConf("passphrase", QByteArray()).toByteArray();

	if (! crt.isEmpty()) {
		qscCert = QSslCertificate(crt);
		if (qscCert.isNull()) {
			log("Failed to parse certificate.");
		} else if (qscCert.issuerInfo(QSslCertificate::CommonName) == QLatin1String("Murmur Autogenerated Certificate")) {
			log("Old autogenerated certificate is unusable for registration, invalidating it");
			qscCert = QSslCertificate();
		}
	}

	if (! key.isEmpty() && qscCert.isNull()) {
		qscCert = QSslCertificate(key);
		if (! qscCert.isNull()) {
			log("Using certificate from key.");
		}
	}

	if (! qscCert.isNull()) {
		QSsl::KeyAlgorithm alg = qscCert.publicKey().algorithm();

		if (! key.isEmpty()) {
			qskKey = QSslKey(key, alg, QSsl::Pem, QSsl::PrivateKey, pass);
			if (qskKey.isNull()) {
				log("Failed to parse key.");
			}
		}

		if (! crt.isEmpty() && qskKey.isNull()) {
			qskKey = QSslKey(crt, alg, QSsl::Pem, QSsl::PrivateKey, pass);
			if (! qskKey.isNull()) {
				log("Using key from certificate.");
			}
		}

	}

	if (qscCert.isNull() || qskKey.isNull()) {
		if (! key.isEmpty() || ! crt.isEmpty()) {
			log("Certificate specified, but failed to load.");
		}
		qskKey = Meta::mp.qskKey;
		qscCert = Meta::mp.qscCert;
		if (qscCert.isNull() || qskKey.isNull()) {
			log("Generating new server certificate.");

			BIO *bio_err;

			CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ON);

			bio_err=BIO_new_fp(stderr, BIO_NOCLOSE);

			X509 *x509 = X509_new();
			EVP_PKEY *pkey = EVP_PKEY_new();
			RSA *rsa = RSA_generate_key(1024,RSA_F4,NULL,NULL);
			EVP_PKEY_assign_RSA(pkey, rsa);

			X509_set_version(x509, 2);
			ASN1_INTEGER_set(X509_get_serialNumber(x509),1);
			X509_gmtime_adj(X509_get_notBefore(x509),0);
			X509_gmtime_adj(X509_get_notAfter(x509),60*60*24*365);
			X509_set_pubkey(x509, pkey);

			X509_NAME *name=X509_get_subject_name(x509);

			X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<unsigned char *>(const_cast<char *>("Murmur Autogenerated Certificate v2")), -1, -1, 0);
			X509_set_issuer_name(x509, name);
			add_ext(x509, NID_basic_constraints, "critical,CA:FALSE");
			add_ext(x509, NID_ext_key_usage, "serverAuth,clientAuth");
			add_ext(x509, NID_subject_key_identifier, "hash");
			add_ext(x509, NID_netscape_comment, "Generated from murmur");

			X509_sign(x509, pkey, EVP_md5());

			crt.resize(i2d_X509(x509, NULL));
			unsigned char *dptr=reinterpret_cast<unsigned char *>(crt.data());
			i2d_X509(x509, &dptr);

			qscCert = QSslCertificate(crt, QSsl::Der);
			if (qscCert.isNull())
				log("Certificate generation failed");

			key.resize(i2d_PrivateKey(pkey, NULL));
			dptr=reinterpret_cast<unsigned char *>(key.data());
			i2d_PrivateKey(pkey, &dptr);

			qskKey = QSslKey(key, QSsl::Rsa, QSsl::Der);
			if (qskKey.isNull())
				log("Key generation failed");

			setConf("certificate", qscCert.toPem());
			setConf("key", qskKey.toPem());
		}
	}

	QList<QSslCipher> pref;
	foreach(QSslCipher c, QSslSocket::defaultCiphers()) {
		if (c.usedBits() < 128)
			continue;
		pref << c;
	}
	if (pref.isEmpty())
		qFatal("No ciphers of at least 128 bit found");
	QSslSocket::setDefaultCiphers(pref);
}

const QString Server::getDigest() const {
	return QString::fromLatin1(qscCert.digest().toHex());
}
