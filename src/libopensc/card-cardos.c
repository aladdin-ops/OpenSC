/*
 * card-cardos.c: Support for Siemens CardOS based cards and tokens
 * 	(for example Aladdin eToken PRO, Eutron CryptoIdentity IT-SEC)
 *
 * Copyright (c) 2005  Nils Larsch <nils@larsch.net>
 * Copyright (C) 2002  Andreas Jellinghaus <aj@dungeon.inka.de>
 * Copyright (C) 2001  Juha Yrj�l� <juha.yrjola@iki.fi>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "internal.h"
#include "cardctl.h"
#include <ctype.h>
#include <string.h>

#include <opensc/asn1.h>

static const struct sc_card_operations *iso_ops = NULL;

struct sc_card_operations cardos_ops;
static struct sc_card_driver cardos_drv = {
	"Siemens CardOS",
	"cardos",
	&cardos_ops,
	NULL, 0, NULL
};

static struct sc_atr_table cardos_atrs[] = {
	/* 4.0 */
	{ "3b:e2:00:ff:c1:10:31:fe:55:c8:02:9c", NULL, NULL, SC_CARD_TYPE_CARDOS_GENERIC, 0, NULL },
	/* 4.01 */
	{ "3b:f2:98:00:ff:c1:10:31:fe:55:c8:03:15", NULL, NULL, SC_CARD_TYPE_CARDOS_M4_01, 0, NULL },
	/* 4.01a */
	{ "3b:f2:98:00:ff:c1:10:31:fe:55:c8:04:12", NULL, NULL, SC_CARD_TYPE_CARDOS_M4_01, 0, NULL },
	/* M4.2 */
	{ "3b:f2:18:00:ff:c1:0a:31:fe:55:c8:06:8a", NULL, NULL, SC_CARD_TYPE_CARDOS_M4_2, 0, NULL },
	{ "3b:f2:18:00:ff:c1:0a:31:fe:55:c8:06:75", NULL, NULL, SC_CARD_TYPE_CARDOS_M4_2, 0, NULL },
	/* M4.3 */
	{ "3b:f2:18:00:02:c1:0a:31:fe:55:c8:07:76", NULL, NULL, SC_CARD_TYPE_CARDOS_M4_3, 0, NULL },
	{ "3b:f2:18:00:02:c1:0a:31:fe:58:c8:08:74", NULL, NULL, SC_CARD_TYPE_CARDOS_M4_3, 0, NULL },
	/* Italian eID card, postecert */
	{ "3b:e9:00:ff:c1:10:31:fe:55:00:64:05:00:c8:02:31:80:00:47", NULL, NULL, SC_CARD_TYPE_CARDOS_GENERIC, 0, NULL },
	/* Italian eID card, infocamere */
	{ "3b:fb:98:00:ff:c1:10:31:fe:55:00:64:05:20:47:03:31:80:00:90:00:f3", NULL, NULL, SC_CARD_TYPE_CARDOS_GENERIC, 0, NULL },
	/* Another Italian InfocamereCard */
	{ "3b:fc:98:00:ff:c1:10:31:fe:55:c8:03:49:6e:66:6f:63:61:6d:65:72:65:28", NULL, NULL, SC_CARD_TYPE_CARDOS_GENERIC, 0, NULL },
	{ "3b:f4:98:00:ff:c1:10:31:fe:55:4d:34:63:76:b4", NULL, NULL, SC_CARD_TYPE_CARDOS_GENERIC, 0, NULL},
	{ NULL, NULL, NULL, 0, 0, NULL }
};

static int cardos_finish(sc_card_t *card)
{
	return 0;
}

static int cardos_match_card(sc_card_t *card)
{
	int i;

	i = _sc_match_atr(card, cardos_atrs, &card->type);
	if (i < 0)
		return 0;
	return 1;
}

static int cardos_have_2048bit_package(sc_card_t *card)
{
	sc_apdu_t apdu;
        u8        rbuf[SC_MAX_APDU_BUFFER_SIZE];
        int       r;
	const u8  *p = rbuf, *q;
	size_t    len, tlen = 0, ilen = 0;

	sc_format_apdu(card, &apdu, SC_APDU_CASE_2_SHORT, 0xca, 0x01, 0x88);
	apdu.resp    = rbuf;
	apdu.resplen = sizeof(rbuf);
	apdu.lc = 0;
	apdu.le = 256;
	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, r, "APDU transmit failed");

	if ((len = apdu.resplen) == 0)
		/* looks like no package has been installed  */
		return 0;

	while (len != 0) {
		p = sc_asn1_find_tag(card->ctx, p, len, 0xe1, &tlen);
		if (p == NULL)
			return 0;
		q = sc_asn1_find_tag(card->ctx, p, tlen, 0x01, &ilen);
		if (q == NULL || ilen != 4)
			return 0;
		if (q[0] == 0x1c)
			return 1;
		p   += tlen;
		len -= tlen + 2;
	}

	return 0;
}

static int cardos_init(sc_card_t *card)
{
	unsigned long	flags;

	card->name = "CardOS M4";
	card->cla = 0x00;

	/* Set up algorithm info. */
	flags = SC_ALGORITHM_NEED_USAGE
		| SC_ALGORITHM_RSA_RAW
		| SC_ALGORITHM_RSA_HASH_NONE
		| SC_ALGORITHM_ONBOARD_KEY_GEN
		;
	_sc_card_add_rsa_alg(card,  512, flags, 0);
	_sc_card_add_rsa_alg(card,  768, flags, 0);
	_sc_card_add_rsa_alg(card, 1024, flags, 0);

	if (card->type == SC_CARD_TYPE_CARDOS_M4_2) {
		int r = cardos_have_2048bit_package(card);
		if (r < 0)
			return r;
		if (r == 1)
			card->caps |= SC_CARD_CAP_RSA_2048;
		card->caps |= SC_CARD_CAP_APDU_EXT;
	} else if (card->type == SC_CARD_TYPE_CARDOS_M4_3) {
		card->caps |= SC_CARD_CAP_RSA_2048;
		card->caps |= SC_CARD_CAP_APDU_EXT;
	}

	if (card->caps & SC_CARD_CAP_RSA_2048) {
		_sc_card_add_rsa_alg(card, 1280, flags, 0);
		_sc_card_add_rsa_alg(card, 1536, flags, 0);
		_sc_card_add_rsa_alg(card, 1792, flags, 0);
		_sc_card_add_rsa_alg(card, 2048, flags, 0);
	}

	return 0;
}

static const struct sc_card_error cardos_errors[] = {
/* some error inside the card */
/* i.e. nothing you can do */
{ 0x6581, SC_ERROR_MEMORY_FAILURE,	"EEPROM error; command aborted"}, 
{ 0x6fff, SC_ERROR_CARD_CMD_FAILED,	"internal assertion error"},
{ 0x6700, SC_ERROR_WRONG_LENGTH,	"LC invalid"}, 
{ 0x6985, SC_ERROR_CARD_CMD_FAILED,	"no random number available"}, 
{ 0x6f81, SC_ERROR_CARD_CMD_FAILED,	"file invalid, maybe checksum error"}, 
{ 0x6f82, SC_ERROR_CARD_CMD_FAILED,	"not enough memory in xram"}, 
{ 0x6f84, SC_ERROR_CARD_CMD_FAILED,	"general protection fault"}, 

/* the card doesn't now thic combination of ins+cla+p1+p2 */
/* i.e. command will never work */
{ 0x6881, SC_ERROR_NO_CARD_SUPPORT,	"logical channel not supported"}, 
{ 0x6a86, SC_ERROR_INCORRECT_PARAMETERS,"p1/p2 invalid"}, 
{ 0x6d00, SC_ERROR_INS_NOT_SUPPORTED,	"ins invalid"}, 
{ 0x6e00, SC_ERROR_CLASS_NOT_SUPPORTED,	"class invalid (hi nibble)"}, 

/* known command, but incorrectly used */
/* i.e. command could work, but you need to change something */
{ 0x6981, SC_ERROR_CARD_CMD_FAILED,	"command cannot be used for file structure"}, 
{ 0x6a80, SC_ERROR_INCORRECT_PARAMETERS,"invalid parameters in data field"}, 
{ 0x6a81, SC_ERROR_NOT_SUPPORTED,	"function/mode not supported"}, 
{ 0x6a85, SC_ERROR_INCORRECT_PARAMETERS,"lc does not fit the tlv structure"}, 
{ 0x6986, SC_ERROR_INCORRECT_PARAMETERS,"no current ef selected"}, 
{ 0x6a87, SC_ERROR_INCORRECT_PARAMETERS,"lc does not fit p1/p2"}, 
{ 0x6c00, SC_ERROR_WRONG_LENGTH,	"le does not fit the data to be sent"}, 
{ 0x6f83, SC_ERROR_CARD_CMD_FAILED,	"command must not be used in transaction"}, 

/* (something) not found */
{ 0x6987, SC_ERROR_INCORRECT_PARAMETERS,"key object for sm not found"}, 
{ 0x6f86, SC_ERROR_CARD_CMD_FAILED,	"key object not found"}, 
{ 0x6a82, SC_ERROR_FILE_NOT_FOUND,	"file not found"}, 
{ 0x6a83, SC_ERROR_RECORD_NOT_FOUND,	"record not found"}, 
{ 0x6a88, SC_ERROR_CARD_CMD_FAILED,	"object not found"}, 

/* (something) invalid */
{ 0x6884, SC_ERROR_CARD_CMD_FAILED,	"chaining error"}, 
{ 0x6984, SC_ERROR_CARD_CMD_FAILED,	"bs object has invalid format"}, 
{ 0x6988, SC_ERROR_INCORRECT_PARAMETERS,"key object used for sm has invalid format"}, 

/* (something) deactivated */
{ 0x6283, SC_ERROR_CARD_CMD_FAILED,	"file is deactivated"	},
{ 0x6983, SC_ERROR_AUTH_METHOD_BLOCKED,	"bs object blocked"}, 

/* access denied */
{ 0x6300, SC_ERROR_SECURITY_STATUS_NOT_SATISFIED,"authentication failed"}, 
{ 0x6982, SC_ERROR_SECURITY_STATUS_NOT_SATISFIED,"required access right not granted"}, 

/* other errors */
{ 0x6a84, SC_ERROR_CARD_CMD_FAILED,	"not enough memory"}, 

/* command ok, execution failed */
{ 0x6f00, SC_ERROR_CARD_CMD_FAILED,	"technical error (see eToken developers guide)"}, 

/* no error, maybe a note */
{ 0x9000, SC_NO_ERROR,		NULL}, 
{ 0x9001, SC_NO_ERROR,		"success, but eeprom weakness detected"}, 
{ 0x9850, SC_NO_ERROR,		"over/underflow useing in/decrease"}
};

static int cardos_check_sw(sc_card_t *card, unsigned int sw1, unsigned int sw2)
{
	const int err_count = sizeof(cardos_errors)/sizeof(cardos_errors[0]);
	int i;
			        
	for (i = 0; i < err_count; i++) {
		if (cardos_errors[i].SWs == ((sw1 << 8) | sw2)) {
			if ( cardos_errors[i].errorstr ) 
				sc_error(card->ctx, "%s\n",
				 	cardos_errors[i].errorstr);
			return cardos_errors[i].errorno;
		}
	}

        sc_error(card->ctx, "Unknown SWs; SW1=%02X, SW2=%02X\n", sw1, sw2);
	return SC_ERROR_CARD_CMD_FAILED;
}

static int cardos_list_files(sc_card_t *card, u8 *buf, size_t buflen)
{
	sc_apdu_t apdu;
	u8        rbuf[256], offset = 0;
	const u8  *p = rbuf, *q;
	int       r;
	size_t    fids = 0, len;

	SC_FUNC_CALLED(card->ctx, 1);

	/* 0x16: DIRECTORY */
	/* 0x02: list both DF and EF */

get_next_part:
	sc_format_apdu(card, &apdu, SC_APDU_CASE_2_SHORT, 0x16, 0x02, offset);
	apdu.cla = 0x80;
	apdu.le = 256;
	apdu.resplen = 256;
	apdu.resp = rbuf;

	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, r, "APDU transmit failed");
	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	SC_TEST_RET(card->ctx, r, "DIRECTORY command returned error");

	if (apdu.resplen > 256) {
		sc_error(card->ctx, "directory listing > 256 bytes, cutting");
		r = 256;
	}

	len = apdu.resplen;
	while (len != 0) {
		size_t   tlen = 0, ilen = 0;
		/* is there a file informatin block (0x6f) ? */
		p = sc_asn1_find_tag(card->ctx, p, len, 0x6f, &tlen);
		if (p == NULL) {
			sc_error(card->ctx, "directory tag missing");
			return SC_ERROR_INTERNAL;
		}
		if (tlen == 0)
			/* empty directory */
			break;
		q = sc_asn1_find_tag(card->ctx, p, tlen, 0x86, &ilen);
		if (q == NULL || ilen != 2) {
			sc_error(card->ctx, "error parsing file id TLV object");
			return SC_ERROR_INTERNAL;
		}
		/* put file id in buf */
		if (buflen >= 2) {
			buf[fids++] = q[0];
			buf[fids++] = q[1];
			buflen -= 2;
		} else
			/* not enought space left in buffer => break */
			break;
		/* extract next offset */
		q = sc_asn1_find_tag(card->ctx, p, tlen, 0x8a, &ilen);
		if (q != NULL && ilen == 1) {
			offset = (u8)ilen;
			if (offset != 0)
				goto get_next_part;
		}
		len -= tlen + 2;
		p   += tlen;
	}

	r = fids;

	SC_FUNC_RETURN(card->ctx, 1, r);
}

static void add_acl_entry(sc_file_t *file, int op, u8 byte)
{
	unsigned int method, key_ref = SC_AC_KEY_REF_NONE;

	switch (byte) {
	case 0x00:
		method = SC_AC_NONE;
		break;
	case 0xFF:
		method = SC_AC_NEVER;
		break;
	default:
		if (byte > 0x7F) {
			method = SC_AC_UNKNOWN;
		} else {
			method = SC_AC_CHV;
			key_ref = byte;
		}
		break;
	}
	sc_file_add_acl_entry(file, op, method, key_ref);
}

static int acl_to_byte(const sc_acl_entry_t *e)
{
	if (e != NULL) {
		switch (e->method) {
		case SC_AC_NONE:
			return 0x00;
		case SC_AC_NEVER:
			return 0xFF;
		case SC_AC_CHV:
		case SC_AC_TERM:
		case SC_AC_AUT:
			if (e->key_ref == SC_AC_KEY_REF_NONE)
				return -1;
			if (e->key_ref > 0x7F)
				return -1;
			return e->key_ref;
		}
	}
        return 0x00;
}

static const int df_acl[9] = {
	-1,			/* LCYCLE (life cycle change) */
	SC_AC_OP_UPDATE,	/* UPDATE Objects */
	-1,			/* APPEND Objects */

	SC_AC_OP_INVALIDATE,	/* DF */
	SC_AC_OP_REHABILITATE,	/* DF */
	SC_AC_OP_DELETE,	/* DF */

	-1,			/* ADMIN DF */
	SC_AC_OP_CREATE,	/* Files */
	-1			/* Reserved */
};
static const int ef_acl[9] = {
	SC_AC_OP_READ,		/* Data */
	SC_AC_OP_UPDATE,	/* Data (write file content) */
	SC_AC_OP_WRITE,		/* */

	SC_AC_OP_INVALIDATE,	/* EF */
	SC_AC_OP_REHABILITATE,	/* EF */
	SC_AC_OP_DELETE,	/* (delete) EF */

	/* XXX: ADMIN should be an ACL type of its own, or mapped
	 * to erase */
	-1,			/* ADMIN EF (modify meta information?) */
	-1,			/* INC (-> cylic fixed files) */
	-1			/* DEC */
};

static void parse_sec_attr(sc_file_t *file, const u8 *buf, size_t len)
{
	size_t i;
	const int *idx;

	idx = (file->type == SC_FILE_TYPE_DF) ?  df_acl : ef_acl;

	/* acl defaults to 0xFF if unspecified */
	for (i = 0; i < 9; i++)
		if (idx[i] != -1)
			add_acl_entry(file, idx[i], (u8)((i < len) ? buf[i] : 0xFF));
}

static int cardos_select_file(sc_card_t *card,
			      const sc_path_t *in_path,
			      sc_file_t **file)
{
	int r;
	
	SC_FUNC_CALLED(card->ctx, 1);
	r = iso_ops->select_file(card, in_path, file);
	if (r >= 0 && file)
		parse_sec_attr((*file), (*file)->sec_attr, (*file)->sec_attr_len);
	SC_FUNC_RETURN(card->ctx, 1, r);
}

static int cardos_acl_to_bytes(sc_card_t *card, const sc_file_t *file,
	u8 *buf, size_t *outlen)
{
	int       i, byte;
	const int *idx;

	if (buf == NULL || *outlen < 9)
		return SC_ERROR_INVALID_ARGUMENTS;

	idx = (file->type == SC_FILE_TYPE_DF) ?  df_acl : ef_acl;
	for (i = 0; i < 9; i++) {
		if (idx[i] < 0)
			byte = 0x00;
		else
			byte = acl_to_byte(sc_file_get_acl_entry(file, idx[i]));
		if (byte < 0) {
			sc_error(card->ctx, "Invalid ACL\n");
			return SC_ERROR_INVALID_ARGUMENTS;
		}
		buf[i] = byte;
	}
	*outlen = 9;

	return SC_SUCCESS;
}

static int cardos_set_file_attributes(sc_card_t *card, sc_file_t *file)
{
	int r;

	if (file->type_attr_len == 0) {
		u8 type[3];

		memset(type, 0, sizeof(type));
		type[0] = 0x00;
		switch (file->type) {
		case SC_FILE_TYPE_WORKING_EF:
			break;
		case SC_FILE_TYPE_DF:
			type[0] = 0x38;
			break;
		default:
			return SC_ERROR_NOT_SUPPORTED;
		}
		if (file->type != SC_FILE_TYPE_DF) {
			switch (file->ef_structure) {
			case SC_FILE_EF_LINEAR_FIXED_TLV:
			case SC_FILE_EF_LINEAR_VARIABLE:
			case SC_FILE_EF_CYCLIC_TLV:
				return SC_ERROR_NOT_SUPPORTED;
				/* No idea what this means, but it
				 * seems to be required for key
				 * generation. */
			case SC_FILE_EF_LINEAR_VARIABLE_TLV:
				type[1] = 0xff;
			default:
				type[0] |= file->ef_structure & 7;
				break;
			}
		}
		r = sc_file_set_type_attr(file, type, sizeof(type));
		if (r != SC_SUCCESS)
			return r;
	}
	if (file->prop_attr_len == 0) {
		u8 status[3];

		status[0] = 0x01;
		if (file->type == SC_FILE_TYPE_DF) {
			status[1] = file->size >> 8;
			status[2] = file->size;
		} else {
			status[1] = status[2] = 0x00; /* not used */
		}
		r = sc_file_set_prop_attr(file, status, sizeof(status));
		if (r != SC_SUCCESS)
			return r;
	}
	if (file->sec_attr_len == 0) {
		u8     acl[9];
		size_t blen = sizeof(acl);

		r = cardos_acl_to_bytes(card, file, acl, &blen);
		if (r != SC_SUCCESS)
			return r;
		r = sc_file_set_sec_attr(file, acl, blen);
		if (r != SC_SUCCESS)
			return r;
	}
	return SC_SUCCESS;
}

/* newer versions of cardos seems to prefer the FCP */
static int cardos_construct_fcp(sc_card_t *card, const sc_file_t *file,
	u8 *out, size_t *outlen)
{
	u8     buf[64], *p = out;
	size_t inlen = *outlen, len;
	int    r;

	SC_FUNC_CALLED(card->ctx, 2);

	if (out == NULL || inlen < 64)
		return SC_ERROR_INVALID_ARGUMENTS;
	/* add FCP tag */
	*p++ = 0x62;
	/* we will add the length later  */
	p++;

	/* set the length */
	buf[0] = (file->size >> 8) & 0xff;
	buf[1] = file->size        & 0xff;
	if (file->type == SC_FILE_TYPE_DF)
		r = sc_asn1_put_tag(0x81, buf, 2, p, 4, &p);
	else
		r = sc_asn1_put_tag(0x80, buf, 2, p, 4, &p);
	if (r != SC_SUCCESS)
		return r;
	/* set file type  */
	if (file->shareable != 0)
		buf[0] = 0x40;
	else
		buf[0] = 0x00;
	if (file->type == SC_FILE_TYPE_WORKING_EF) {
		switch (file->ef_structure) {
		case SC_FILE_EF_TRANSPARENT:
			buf[0] |= 0x01;
			break;
		case SC_FILE_EF_LINEAR_VARIABLE_TLV:
			buf[0] |= 0x05;
			break;
		case SC_FILE_EF_LINEAR_FIXED:
			buf[0] |= 0x02;
			buf[1] |= 0x21;
			buf[2] |= 0x00;
			buf[3] |= (u8) file->record_length;
			buf[4] |= (u8) file->record_count;
			break;
		case SC_FILE_EF_CYCLIC:
			buf[0] |= 0x06;
			buf[1] |= 0x21;
			buf[2] |= 0x00;
			buf[3] |= (u8) file->record_length;
			buf[4] |= (u8) file->record_count;
			break;
		default:
			sc_error(card->ctx, "unknown EF type: %u", file->type);
			return SC_ERROR_INVALID_ARGUMENTS;
		}
		if (file->ef_structure == SC_FILE_EF_CYCLIC ||
		    file->ef_structure == SC_FILE_EF_LINEAR_FIXED)
		r = sc_asn1_put_tag(0x82, buf, 5, p, 8, &p);
	else
		r = sc_asn1_put_tag(0x82, buf, 1, p, 8, &p);
	} else if (file->type == SC_FILE_TYPE_DF) {
		buf[0] |= 0x38;
		r = sc_asn1_put_tag(0x82, buf, 1, p, 8, &p);
	} else
		return SC_ERROR_NOT_SUPPORTED;
	if (r != SC_SUCCESS)
		return r;
	/* set file id */
	buf[0] = (file->id >> 8) & 0xff;
	buf[1] = file->id        & 0xff;
	r = sc_asn1_put_tag(0x83, buf, 2, p, 8, &p);
	if (r != SC_SUCCESS)
		return r;
	/* set aid (for DF only) */
	if (file->type == SC_FILE_TYPE_DF && file->namelen != 0) {
		r = sc_asn1_put_tag(0x84, file->name, file->namelen, p, 20, &p);
		if (r != SC_SUCCESS)
			return r;
	}
	/* set proprietary file attributes */
	buf[0] = 0x00;		/* use default values */
	if (file->type == SC_FILE_TYPE_DF)
		r = sc_asn1_put_tag(0x85, buf, 1, p, 8, &p);
	else {
		buf[1] = 0x00;
		buf[2] = 0x00;
		r = sc_asn1_put_tag(0x85, buf, 1, p, 8, &p);
	}
	if (r != SC_SUCCESS)
		return r;
	/* set ACs  */
	len = 9;
	r = cardos_acl_to_bytes(card, file, buf, &len);
	if (r != SC_SUCCESS)
		return r;
	r = sc_asn1_put_tag(0x86, buf, len, p, 18, &p);
	if (r != SC_SUCCESS)
		return r;
	/* finally set the length of the FCP */
	out[1] = p - out - 2;

	*outlen = p - out;

	return SC_SUCCESS;
}

static int cardos_create_file(sc_card_t *card, sc_file_t *file)
{
	int       r;

	SC_FUNC_CALLED(card->ctx, 1);

	if (card->type == SC_CARD_TYPE_CARDOS_GENERIC ||
	    card->type == SC_CARD_TYPE_CARDOS_M4_01) {
		r = cardos_set_file_attributes(card, file);
		if (r != SC_SUCCESS)
			return r;
		return iso_ops->create_file(card, file);
	} else if (card->type == SC_CARD_TYPE_CARDOS_M4_2 ||
	           card->type == SC_CARD_TYPE_CARDOS_M4_3) {
		u8        sbuf[SC_MAX_APDU_BUFFER_SIZE];
		size_t    len = sizeof(sbuf);
		sc_apdu_t apdu;

		r = cardos_construct_fcp(card, file, sbuf, &len);
		if (r < 0) {
			sc_error(card->ctx, "unable to create FCP");
			return r;
		}
	
		sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0xE0, 0x00, 0x00);
		apdu.lc      = len;
		apdu.datalen = len;
		apdu.data    = sbuf;

		r = sc_transmit_apdu(card, &apdu);
		SC_TEST_RET(card->ctx, r, "APDU transmit failed");

		return sc_check_sw(card, apdu.sw1, apdu.sw2);
	} else
		return SC_ERROR_NOT_SUPPORTED;
}

/*
 * Restore the indicated SE
 */
static int
cardos_restore_security_env(sc_card_t *card, int se_num)
{
	sc_apdu_t apdu;
	int	r;

	SC_FUNC_CALLED(card->ctx, 1);

	sc_format_apdu(card, &apdu, SC_APDU_CASE_1, 0x22, 3, se_num);

	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, r, "APDU transmit failed");

	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	SC_TEST_RET(card->ctx, r, "Card returned error");

	SC_FUNC_RETURN(card->ctx, 1, r);
}

/*
 * Set the security context
 * Things get a little messy here. It seems you cannot do any
 * crypto without a security environment - but there isn't really
 * a way to specify the security environment in PKCS15.
 * What I'm doing here (for now) is to assume that for a key
 * object with ID 0xNN there is always a corresponding SE object
 * with the same ID.
 * XXX Need to find out how the Aladdin drivers do it.
 */
static int
cardos_set_security_env(sc_card_t *card,
			    const sc_security_env_t *env,
			    int se_num)
{
	sc_apdu_t apdu;
	u8	data[3];
	int	key_id, r;

	assert(card != NULL && env != NULL);

	if (!(env->flags & SC_SEC_ENV_KEY_REF_PRESENT)
	 || env->key_ref_len != 1) {
		sc_error(card->ctx, "No or invalid key reference\n");
		return SC_ERROR_INVALID_ARGUMENTS;
	}
	key_id = env->key_ref[0];

	sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0x22, 1, 0);
	switch (env->operation) {
	case SC_SEC_OPERATION_DECIPHER:
		apdu.p2 = 0xB8;
		break;
	case SC_SEC_OPERATION_SIGN:
		apdu.p2 = 0xB6;
		break;
	default:
		return SC_ERROR_INVALID_ARGUMENTS;
	}

	data[0] = 0x83;
	data[1] = 0x01;
	data[2] = key_id;
	apdu.lc = apdu.datalen = 3;
	apdu.data = data;

	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, r, "APDU transmit failed");

	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	SC_TEST_RET(card->ctx, r, "Card returned error");

	SC_FUNC_RETURN(card->ctx, 1, r);
}

/*
 * Compute digital signature
 */

/* internal function to do the actual signature computation */
static int
do_compute_signature(sc_card_t *card, const u8 *data, size_t datalen,
		     u8 *out, size_t outlen)
{
	int r;
	sc_apdu_t apdu;

	/* INS: 0x2A  PERFORM SECURITY OPERATION
	 * P1:  0x9E  Resp: Digital Signature
	 * P2:  0x9A  Cmd: Input for Digital Signature */
	sc_format_apdu(card, &apdu, SC_APDU_CASE_4, 0x2A, 0x9E, 0x9A);
	apdu.resp    = out;
	apdu.le      = outlen;
	apdu.resplen = outlen;

	apdu.data    = data;
	apdu.lc      = datalen;
	apdu.datalen = datalen;
	apdu.sensitive = 1;
	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, r, "APDU transmit failed");

	if (apdu.sw1 == 0x90 && apdu.sw2 == 0x00)
		SC_FUNC_RETURN(card->ctx, 4, apdu.resplen)
	else
		SC_FUNC_RETURN(card->ctx, 4, sc_check_sw(card, apdu.sw1, apdu.sw2))
}

static int
cardos_compute_signature(sc_card_t *card, const u8 *data, size_t datalen,
			 u8 *out, size_t outlen)
{
	int    r;
	u8     buf[SC_MAX_APDU_BUFFER_SIZE];
	size_t buf_len = sizeof(buf), tmp_len = buf_len;
	sc_context_t *ctx;

	assert(card != NULL && data != NULL && out != NULL);	
	ctx = card->ctx;
	SC_FUNC_CALLED(ctx, 1);

	if (datalen > SC_MAX_APDU_BUFFER_SIZE)
		SC_FUNC_RETURN(card->ctx, 4, SC_ERROR_INVALID_ARGUMENTS);
	if (outlen < datalen)
		SC_FUNC_RETURN(card->ctx, 4, SC_ERROR_BUFFER_TOO_SMALL);
	outlen = datalen;

	/* XXX As we don't know what operations are allowed with a
	 * certain key, let's try RSA_PURE etc. and see which operation
	 * succeeds (this is not really beautiful, but currently the
	 * only way I see) -- Nils
	 */
	if (ctx->debug >= 3)
		sc_debug(ctx, "trying RSA_PURE_SIG (padded DigestInfo)\n");
	sc_ctx_suppress_errors_on(ctx);
	r = do_compute_signature(card, data, datalen, out, outlen);
	sc_ctx_suppress_errors_off(ctx);
	if (r >= SC_SUCCESS)
		SC_FUNC_RETURN(ctx, 4, r);
	if (ctx->debug >= 3)
		sc_debug(ctx, "trying RSA_SIG (just the DigestInfo)\n");
	/* remove padding: first try pkcs1 bt01 padding */
	r = sc_pkcs1_strip_01_padding(data, datalen, buf, &tmp_len);
	if (r != SC_SUCCESS) {
		/* no pkcs1 bt01 padding => let's try zero padding */
		tmp_len = buf_len;
		r = sc_strip_zero_padding(data, datalen, buf, &tmp_len);
		if (r != SC_SUCCESS)
			SC_FUNC_RETURN(ctx, 4, r);
	}
	sc_ctx_suppress_errors_on(ctx);
	r = do_compute_signature(card, buf, tmp_len, out, outlen);
	sc_ctx_suppress_errors_off(ctx);
	if (r >= SC_SUCCESS)	
		SC_FUNC_RETURN(ctx, 4, r);
	if (ctx->debug >= 3)
		sc_debug(ctx, "trying to sign raw hash value\n");
	r = sc_pkcs1_strip_digest_info_prefix(NULL,buf,tmp_len,buf,&buf_len);
	if (r != SC_SUCCESS)
		SC_FUNC_RETURN(ctx, 4, r);
	return do_compute_signature(card, buf, buf_len, out, outlen);
}

static int
cardos_lifecycle_get(sc_card_t *card, int *mode)
{
	sc_apdu_t	apdu;
	u8 rbuf[SC_MAX_APDU_BUFFER_SIZE];
	int		r;

	SC_FUNC_CALLED(card->ctx, 1);

	sc_format_apdu(card, &apdu, SC_APDU_CASE_2_SHORT, 0xca, 0x01, 0x83);
	apdu.cla = 0x00;
	apdu.le = 256;
	apdu.resplen = sizeof(rbuf);
	apdu.resp = rbuf;

	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, r, "APDU transmit failed");

	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	SC_TEST_RET(card->ctx, r, "Card returned error");

	if (apdu.resplen < 1) {
		SC_TEST_RET(card->ctx, r, "Lifecycle byte not in response");
	}

	r = SC_SUCCESS;
	switch (rbuf[0]) {
	case 0x10:
		*mode = SC_CARDCTRL_LIFECYCLE_USER;
		break;
	case 0x20:
		*mode = SC_CARDCTRL_LIFECYCLE_ADMIN;
		break;
	case 0x34: /* MANUFACTURING */
		*mode = SC_CARDCTRL_LIFECYCLE_OTHER;
		break;
	default:
		sc_error(card->ctx, "Unknown lifecycle byte %d", rbuf[0]);
		r = SC_ERROR_INTERNAL;
	}

	SC_FUNC_RETURN(card->ctx, 1, r);
}

static int
cardos_lifecycle_set(sc_card_t *card, int *mode)
{
	sc_apdu_t	apdu;
	int		r;

	int current;
	int target;

	SC_FUNC_CALLED(card->ctx, 1);

	target = *mode;

	r = cardos_lifecycle_get(card, &current);
	
	if (r != SC_SUCCESS)
		return r;

	if (current == target || current == SC_CARDCTRL_LIFECYCLE_OTHER)
		return SC_SUCCESS;

	sc_format_apdu(card, &apdu, SC_APDU_CASE_1, 0x10, 0, 0);
	apdu.cla = 0x80;
	apdu.le = 0;
	apdu.resplen = 0;
	apdu.resp = NULL;

	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, r, "APDU transmit failed");

	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	SC_TEST_RET(card->ctx, r, "Card returned error");

	SC_FUNC_RETURN(card->ctx, 1, r);
}

static int
cardos_put_data_oci(sc_card_t *card,
			struct sc_cardctl_cardos_obj_info *args)
{
	sc_apdu_t	apdu;
	int		r;

	SC_FUNC_CALLED(card->ctx, 1);

	memset(&apdu, 0, sizeof(apdu));
	apdu.cse = SC_APDU_CASE_3_SHORT;
	apdu.cla = 0x00;
	apdu.ins = 0xda;
	apdu.p1  = 0x01;
	apdu.p2  = 0x6e;
	apdu.lc  = args->len;
	apdu.data = args->data;
	apdu.datalen = args->len;

	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, r, "APDU transmit failed");

	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	SC_TEST_RET(card->ctx, r, "Card returned error");

	SC_FUNC_RETURN(card->ctx, 1, r);
}

static int
cardos_put_data_seci(sc_card_t *card,
			struct sc_cardctl_cardos_obj_info *args)
{
	sc_apdu_t	apdu;
	int		r;

	memset(&apdu, 0, sizeof(apdu));
	apdu.cse = SC_APDU_CASE_3_SHORT;
	apdu.cla = 0x00;
	apdu.ins = 0xda;
	apdu.p1  = 0x01;
	apdu.p2  = 0x6d;
	apdu.lc  = args->len;
	apdu.data = args->data;
	apdu.datalen = args->len;

	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, r, "APDU transmit failed");

	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	SC_TEST_RET(card->ctx, r, "Card returned error");

	return r;
}

static int
cardos_generate_key(sc_card_t *card,
		struct sc_cardctl_cardos_genkey_info *args)
{
	sc_apdu_t	apdu;
	u8		data[8];
	int		r;

	data[0] = 0x20;		/* store as PSO object */
	data[1] = args->key_id;
	data[2] = args->fid >> 8;
	data[3] = args->fid & 0xff;
	data[4] = 0;		/* additional Rabin Miller tests */
	data[5] = 0x10;		/* length difference between p, q (bits) */
	data[6] = 0;		/* default length of exponent, MSB */
	data[7] = 0x20;		/* default length of exponent, LSB */

	memset(&apdu, 0, sizeof(apdu));
	apdu.cse = SC_APDU_CASE_3_SHORT;
	apdu.cla = 0x00;
	apdu.ins = 0x46;
	apdu.p1  = 0x00;
	apdu.p2  = 0x00;
	apdu.data= data;
	apdu.datalen = apdu.lc = sizeof(data);

	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, r, "APDU transmit failed");
	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	SC_TEST_RET(card->ctx, r, "GENERATE_KEY failed");

	return r;
}

static int cardos_get_serialnr(sc_card_t *card, sc_serial_number_t *serial)
{
	int r;
	sc_apdu_t apdu;
	u8  rbuf[SC_MAX_APDU_BUFFER_SIZE];

	sc_format_apdu(card, &apdu, SC_APDU_CASE_2_SHORT, 0xca, 0x01, 0x81);
	apdu.resp = rbuf;
	apdu.resplen = sizeof(rbuf);
	apdu.le   = 256;
	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, r,  "APDU transmit failed");
	if (apdu.sw1 != 0x90 || apdu.sw2 != 0x00)
		return SC_ERROR_INTERNAL;
	if (apdu.resplen != 32) {
		sc_debug(card->ctx, "unexpected response to GET DATA serial"
				" number\n");
		return SC_ERROR_INTERNAL;
	}
	/* cache serial number */
	memcpy(card->serialnr.value, &rbuf[10], 6);
	card->serialnr.len = 6;
	/* copy and return serial number */
	memcpy(serial, &card->serialnr, sizeof(*serial));
	return SC_SUCCESS;
}

static int
cardos_card_ctl(sc_card_t *card, unsigned long cmd, void *ptr)
{
	switch (cmd) {
	case SC_CARDCTL_CARDOS_PUT_DATA_FCI:
		break;
	case SC_CARDCTL_CARDOS_PUT_DATA_OCI:
		return cardos_put_data_oci(card,
			(struct sc_cardctl_cardos_obj_info *) ptr);
		break;
	case SC_CARDCTL_CARDOS_PUT_DATA_SECI:
		return cardos_put_data_seci(card,
			(struct sc_cardctl_cardos_obj_info *) ptr);
		break;
	case SC_CARDCTL_CARDOS_GENERATE_KEY:
		return cardos_generate_key(card,
			(struct sc_cardctl_cardos_genkey_info *) ptr);
	case SC_CARDCTL_LIFECYCLE_GET:
		return cardos_lifecycle_get(card, (int *) ptr);
	case SC_CARDCTL_LIFECYCLE_SET:
		return cardos_lifecycle_set(card, (int *) ptr);
	case SC_CARDCTL_GET_SERIALNR:
		return cardos_get_serialnr(card, (sc_serial_number_t *)ptr);
	}
	return SC_ERROR_NOT_SUPPORTED;
}

/*
 * The 0x80 thing tells the card it's okay to search parent
 * directories as well for the referenced object.
 * Unfortunately, it doesn't seem to work without this flag :-/
 */
static int
cardos_pin_cmd(sc_card_t *card, struct sc_pin_cmd_data *data,
		 int *tries_left)
{
	data->flags |= SC_PIN_CMD_NEED_PADDING;
	data->pin_reference |= 0x80;
	/* FIXME: the following values depend on what pin length was
	 * used when creating the BS objects */
	if (data->pin1.max_length == 0)
		data->pin1.max_length = 8;
	if (data->pin2.max_length == 0)
		data->pin2.max_length = 8;
	return iso_ops->pin_cmd(card, data, tries_left);
}

static int cardos_logout(sc_card_t *card)
{
	if (card->type == SC_CARD_TYPE_CARDOS_M4_01 ||
	    card->type == SC_CARD_TYPE_CARDOS_M4_2) {
		sc_apdu_t apdu;
		int       r;
		sc_path_t path;

		sc_format_path("3F00", &path);
		r = sc_select_file(card, &path, NULL);
		if (r != SC_SUCCESS)
			return r;

		sc_format_apdu(card, &apdu, SC_APDU_CASE_1, 0xEA, 0x00, 0x00);
		apdu.cla = 0x80;

		r = sc_transmit_apdu(card, &apdu);
		SC_TEST_RET(card->ctx, r, "APDU transmit failed");

		return sc_check_sw(card, apdu.sw1, apdu.sw2);
	} else
		return SC_ERROR_NOT_SUPPORTED;
}



/* eToken R2 supports WRITE_BINARY, PRO Tokens support UPDATE_BINARY */

static struct sc_card_driver * sc_get_driver(void)
{
	if (iso_ops == NULL)
		iso_ops = sc_get_iso7816_driver()->ops;
	cardos_ops = *iso_ops;
	cardos_ops.match_card = cardos_match_card;
	cardos_ops.init = cardos_init;
	cardos_ops.finish = cardos_finish;
	cardos_ops.select_file = cardos_select_file;
	cardos_ops.create_file = cardos_create_file;
	cardos_ops.set_security_env = cardos_set_security_env;
	cardos_ops.restore_security_env = cardos_restore_security_env;
	cardos_ops.compute_signature = cardos_compute_signature;

	cardos_ops.list_files = cardos_list_files;
	cardos_ops.check_sw = cardos_check_sw;
	cardos_ops.card_ctl = cardos_card_ctl;
	cardos_ops.pin_cmd = cardos_pin_cmd;
	cardos_ops.logout  = cardos_logout;

	return &cardos_drv;
}

#if 1
struct sc_card_driver * sc_get_cardos_driver(void)
{
	return sc_get_driver();
}
#endif
