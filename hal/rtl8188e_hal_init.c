/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _HAL_INIT_C_

#include <drv_types.h>
#include <rtl8188e_hal.h>
#ifdef CONFIG_SFW_SUPPORTED
#include "hal8188e_s_fw.h"
#endif
#include "hal8188e_t_fw.h"


#if defined(CONFIG_IOL)
static void iol_mode_enable(PADAPTER padapter, u8 enable)
{
	u8 reg_0xf0 = 0;

	if (enable) {
		/* Enable initial offload */
		reg_0xf0 = rtw_read8(padapter, REG_SYS_CFG);
		/* RTW_INFO("%s reg_0xf0:0x%02x, write 0x%02x\n", __func__, reg_0xf0, reg_0xf0|SW_OFFLOAD_EN); */
		rtw_write8(padapter, REG_SYS_CFG, reg_0xf0 | SW_OFFLOAD_EN);

		if (padapter->bFWReady == false) {
			RTW_INFO("bFWReady == false call reset 8051...\n");
			_8051Reset88E(padapter);
		}

	} else {
		/* disable initial offload */
		reg_0xf0 = rtw_read8(padapter, REG_SYS_CFG);
		/* RTW_INFO("%s reg_0xf0:0x%02x, write 0x%02x\n", __func__, reg_0xf0, reg_0xf0& ~SW_OFFLOAD_EN); */
		rtw_write8(padapter, REG_SYS_CFG, reg_0xf0 & ~SW_OFFLOAD_EN);
	}
}

static s32 iol_execute(PADAPTER padapter, u8 control)
{
	s32 status = _FAIL;
	u8 reg_0x88 = 0, reg_1c7 = 0;
	u32 start = 0, passing_time = 0;

	u32 t1, t2;
	control = control & 0x0f;
	reg_0x88 = rtw_read8(padapter, REG_HMEBOX_E0);
	/* RTW_INFO("%s reg_0x88:0x%02x, write 0x%02x\n", __func__, reg_0x88, reg_0x88|control); */
	rtw_write8(padapter, REG_HMEBOX_E0,  reg_0x88 | control);

	t1 = start = rtw_get_current_time();
	while (
		/* (reg_1c7 = rtw_read8(padapter, 0x1c7) >1) && */
		(reg_0x88 = rtw_read8(padapter, REG_HMEBOX_E0)) & control
		&& (passing_time = rtw_get_passing_time_ms(start)) < 1000
	) {
		/* RTW_INFO("%s polling reg_0x88:0x%02x,reg_0x1c7:0x%02x\n", __func__, reg_0x88,rtw_read8(padapter, 0x1c7) ); */
		/* rtw_udelay_os(100); */
	}

	reg_0x88 = rtw_read8(padapter, REG_HMEBOX_E0);
	status = (reg_0x88 & control) ? _FAIL : _SUCCESS;
	if (reg_0x88 & control << 4)
		status = _FAIL;
	t2 = rtw_get_current_time();

	return status;
}

static s32 iol_InitLLTTable(
	PADAPTER padapter,
	u8 txpktbuf_bndy
)
{
	s32 rst = _SUCCESS;
	iol_mode_enable(padapter, 1);
	/* RTW_INFO("%s txpktbuf_bndy:%u\n", __func__, txpktbuf_bndy); */
	rtw_write8(padapter, REG_TDECTRL + 1, txpktbuf_bndy);
	rst = iol_execute(padapter, CMD_INIT_LLT);
	iol_mode_enable(padapter, 0);
	return rst;
}

static void
efuse_phymap_to_logical(u8 *phymap, u16 _offset, u16 _size_byte, u8  *pbuf)
{
	u8	*efuseTbl = NULL;
	u8	rtemp8;
	u16	eFuse_Addr = 0;
	u8	offset, wren;
	u16	i, j;
	u16	**eFuseWord = NULL;
	u16	efuse_utilized = 0;
	u8	efuse_usage = 0;
	u8	u1temp = 0;


	efuseTbl = (u8 *)rtw_zmalloc(EFUSE_MAP_LEN_88E);
	if (efuseTbl == NULL) {
		RTW_INFO("%s: alloc efuseTbl fail!\n", __func__);
		goto exit;
	}

	eFuseWord = (u16 **)rtw_malloc2d(EFUSE_MAX_SECTION_88E, EFUSE_MAX_WORD_UNIT, 2);
	if (eFuseWord == NULL) {
		RTW_INFO("%s: alloc eFuseWord fail!\n", __func__);
		goto exit;
	}

	/* 0. Refresh efuse init map as all oxFF. */
	for (i = 0; i < EFUSE_MAX_SECTION_88E; i++)
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++)
			eFuseWord[i][j] = 0xFFFF;

	/*  */
	/* 1. Read the first byte to check if efuse is empty!!! */
	/*  */
	/*  */
	rtemp8 = *(phymap + eFuse_Addr);
	if (rtemp8 != 0xFF) {
		efuse_utilized++;
		eFuse_Addr++;
	} else {
		RTW_INFO("EFUSE is empty efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, rtemp8);
		goto exit;
	}


	/*  */
	/* 2. Read real efuse content. Filter PG header and every section data. */
	/*  */
	while ((rtemp8 != 0xFF) && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E)) {
		/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("efuse_Addr-%d efuse_data=%x\n", eFuse_Addr-1, *rtemp8)); */

		/* Check PG header for section num. */
		if ((rtemp8 & 0x1F) == 0x0F) {	/* extended header */
			u1temp = ((rtemp8 & 0xE0) >> 5);
			/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header u1temp=%x *rtemp&0xE0 0x%x\n", u1temp, *rtemp8 & 0xE0)); */

			/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header u1temp=%x\n", u1temp)); */

			rtemp8 = *(phymap + eFuse_Addr);

			/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, *rtemp8));	 */

			if ((rtemp8 & 0x0F) == 0x0F) {
				eFuse_Addr++;
				rtemp8 = *(phymap + eFuse_Addr);

				if (rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E))
					eFuse_Addr++;
				continue;
			} else {
				offset = ((rtemp8 & 0xF0) >> 1) | u1temp;
				wren = (rtemp8 & 0x0F);
				eFuse_Addr++;
			}
		} else {
			offset = ((rtemp8 >> 4) & 0x0f);
			wren = (rtemp8 & 0x0f);
		}

		if (offset < EFUSE_MAX_SECTION_88E) {
			/* Get word enable value from PG header */
			/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Offset-%d Worden=%x\n", offset, wren)); */

			for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++) {
				/* Check word enable condition in the section				 */
				if (!(wren & 0x01)) {
					/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d\n", eFuse_Addr)); */
					rtemp8 = *(phymap + eFuse_Addr);
					eFuse_Addr++;
					/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Data=0x%x\n", *rtemp8)); 				 */
					efuse_utilized++;
					eFuseWord[offset][i] = (rtemp8 & 0xff);


					if (eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E)
						break;

					/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d", eFuse_Addr)); */
					rtemp8 = *(phymap + eFuse_Addr);
					eFuse_Addr++;
					/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Data=0x%x\n", *rtemp8)); 				 */

					efuse_utilized++;
					eFuseWord[offset][i] |= (((u16)rtemp8 << 8) & 0xff00);

					if (eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E)
						break;
				}

				wren >>= 1;

			}
		}

		/* Read next PG header */
		rtemp8 = *(phymap + eFuse_Addr);
		/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d rtemp 0x%x\n", eFuse_Addr, *rtemp8)); */

		if (rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E)) {
			efuse_utilized++;
			eFuse_Addr++;
		}
	}

	/*  */
	/* 3. Collect 16 sections and 4 word unit into Efuse map. */
	/*  */
	for (i = 0; i < EFUSE_MAX_SECTION_88E; i++) {
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++) {
			efuseTbl[(i * 8) + (j * 2)] = (eFuseWord[i][j] & 0xff);
			efuseTbl[(i * 8) + ((j * 2) + 1)] = ((eFuseWord[i][j] >> 8) & 0xff);
		}
	}


	/*  */
	/* 4. Copy from Efuse map to output pointer memory!!! */
	/*  */
	for (i = 0; i < _size_byte; i++)
		pbuf[i] = efuseTbl[_offset + i];

	/*  */
	/* 5. Calculate Efuse utilization. */
	/*  */
	efuse_usage = (u8)((efuse_utilized * 100) / EFUSE_REAL_CONTENT_LEN_88E);
	/* rtw_hal_set_hwreg(Adapter, HW_VAR_EFUSE_BYTES, (u8 *)&efuse_utilized); */

exit:
	if (efuseTbl)
		rtw_mfree(efuseTbl, EFUSE_MAP_LEN_88E);

	if (eFuseWord)
		rtw_mfree2d((void *)eFuseWord, EFUSE_MAX_SECTION_88E, EFUSE_MAX_WORD_UNIT, sizeof(u16));
}

static void efuse_read_phymap_from_txpktbuf(
	ADAPTER *adapter,
	int bcnhead,	/* beacon head, where FW store len(2-byte) and efuse physical map. */
	u8 *content,	/* buffer to store efuse physical map */
	u16 *size	/* for efuse content: the max byte to read. will update to byte read */
)
{
	u16 dbg_addr = 0;
	u32 start  = 0, passing_time = 0;
	u8 reg_0x143 = 0;
	u8 reg_0x106 = 0;
	u32 lo32 = 0, hi32 = 0;
	u16 len = 0, count = 0;
	int i = 0;
	u16 limit = *size;

	u8 *pos = content;

	if (bcnhead < 0) /* if not valid */
		bcnhead = rtw_read8(adapter, REG_TDECTRL + 1);

	RTW_INFO("%s bcnhead:%d\n", __func__, bcnhead);

	/* reg_0x106 = rtw_read8(adapter, REG_PKT_BUFF_ACCESS_CTRL); */
	/* RTW_INFO("%s reg_0x106:0x%02x, write 0x%02x\n", __func__, reg_0x106, 0x69); */
	rtw_write8(adapter, REG_PKT_BUFF_ACCESS_CTRL, TXPKT_BUF_SELECT);
	/* RTW_INFO("%s reg_0x106:0x%02x\n", __func__, rtw_read8(adapter, 0x106)); */

	dbg_addr = bcnhead * 128 / 8; /* 8-bytes addressing */

	while (1) {
		/* RTW_INFO("%s dbg_addr:0x%x\n", __func__, dbg_addr+i); */
		rtw_write16(adapter, REG_PKTBUF_DBG_ADDR, dbg_addr + i);

		/* RTW_INFO("%s write reg_0x143:0x00\n", __func__); */
		rtw_write8(adapter, REG_TXPKTBUF_DBG, 0);
		start = rtw_get_current_time();
		while (!(reg_0x143 = rtw_read8(adapter, REG_TXPKTBUF_DBG)) /* dbg */
		       /* while(rtw_read8(adapter, REG_TXPKTBUF_DBG) & BIT0 */
		       && (passing_time = rtw_get_passing_time_ms(start)) < 1000
		      ) {
			RTW_INFO("%s polling reg_0x143:0x%02x, reg_0x106:0x%02x\n", __func__, reg_0x143, rtw_read8(adapter, 0x106));
			rtw_usleep_os(100);
		}


		lo32 = rtw_read32(adapter, REG_PKTBUF_DBG_DATA_L);
		hi32 = rtw_read32(adapter, REG_PKTBUF_DBG_DATA_H);

		if (i == 0) {
			u8 lenc[2];
			u16 lenbak, aaabak;
			u16 aaa;
			lenc[0] = rtw_read8(adapter, REG_PKTBUF_DBG_DATA_L);
			lenc[1] = rtw_read8(adapter, REG_PKTBUF_DBG_DATA_L + 1);

			aaabak = le16_to_cpup((__le16 *)lenc);
			lenbak = le16_to_cpu(*((__le16 *)lenc));
			aaa = le16_to_cpup((__le16 *)&lo32);
			len = le16_to_cpu(*((__le16 *)&lo32));

			limit = (len - 2 < limit) ? len - 2 : limit;

			RTW_INFO("%s len:%u, lenbak:%u, aaa:%u, aaabak:%u\n", __func__, len, lenbak, aaa, aaabak);

			_rtw_memcpy(pos, ((u8 *)&lo32) + 2, (limit >= count + 2) ? 2 : limit - count);
			count += (limit >= count + 2) ? 2 : limit - count;
			pos = content + count;

		} else {
			_rtw_memcpy(pos, ((u8 *)&lo32), (limit >= count + 4) ? 4 : limit - count);
			count += (limit >= count + 4) ? 4 : limit - count;
			pos = content + count;


		}

		if (limit > count && len - 2 > count) {
			_rtw_memcpy(pos, (u8 *)&hi32, (limit >= count + 4) ? 4 : limit - count);
			count += (limit >= count + 4) ? 4 : limit - count;
			pos = content + count;
		}

		if (limit <= count || len - 2 <= count)
			break;

		i++;
	}

	rtw_write8(adapter, REG_PKT_BUFF_ACCESS_CTRL, DISABLE_TRXPKT_BUF_ACCESS);

	RTW_INFO("%s read count:%u\n", __func__, count);
	*size = count;

}


static s32 iol_read_efuse(
	PADAPTER padapter,
	u8 txpktbuf_bndy,
	u16 offset,
	u16 size_byte,
	u8 *logical_map
)
{
	s32 status = _FAIL;
	u8 reg_0x106 = 0;
	u8 physical_map[512];
	u16 size = 512;
	int i;


	rtw_write8(padapter, REG_TDECTRL + 1, txpktbuf_bndy);
	memset(physical_map, 0xFF, 512);

	rtw_write8(padapter, REG_PKT_BUFF_ACCESS_CTRL, TXPKT_BUF_SELECT);

	status = iol_execute(padapter, CMD_READ_EFUSE_MAP);

	if (status == _SUCCESS)
		efuse_read_phymap_from_txpktbuf(padapter, txpktbuf_bndy, physical_map, &size);

	efuse_phymap_to_logical(physical_map, offset, size_byte, logical_map);

	return status;
}

s32 rtl8188e_iol_efuse_patch(PADAPTER padapter)
{
	s32	result = _SUCCESS;

	if (rtw_IOL_applied(padapter)) {
		iol_mode_enable(padapter, 1);
		result = iol_execute(padapter, CMD_READ_EFUSE_MAP);
		if (result == _SUCCESS)
			result = iol_execute(padapter, CMD_EFUSE_PATCH);

		iol_mode_enable(padapter, 0);
	}
	return result;
}

static s32 iol_ioconfig(
	PADAPTER padapter,
	u8 iocfg_bndy
)
{
	s32 rst = _SUCCESS;

	/* RTW_INFO("%s iocfg_bndy:%u\n", __func__, iocfg_bndy); */
	rtw_write8(padapter, REG_TDECTRL + 1, iocfg_bndy);
	rst = iol_execute(padapter, CMD_IOCONFIG);

	return rst;
}

static int rtl8188e_IOL_exec_cmds_sync(ADAPTER *adapter, struct xmit_frame *xmit_frame, u32 max_wating_ms, u32 bndy_cnt)
{

	u32 start_time = rtw_get_current_time();
	u32 passing_time_ms;
	u8 polling_ret, i;
	int ret = _FAIL;
	u32 t1, t2;

	if (rtw_IOL_append_END_cmd(xmit_frame) != _SUCCESS)
		goto exit;
	{
		struct pkt_attrib	*pattrib = &xmit_frame->attrib;
		if (rtw_usb_bulk_size_boundary(adapter, TXDESC_SIZE + pattrib->last_txcmdsz)) {
			if (rtw_IOL_append_END_cmd(xmit_frame) != _SUCCESS)
				goto exit;
		}
	}

	/* rtw_IOL_cmd_buf_dump(adapter,xmit_frame->attrib.pktlen+TXDESC_OFFSET,xmit_frame->buf_addr); */
	/* rtw_hal_mgnt_xmit(adapter, xmit_frame); */
	/* rtw_dump_xframe_sync(adapter, xmit_frame); */

	dump_mgntframe_and_wait(adapter, xmit_frame, max_wating_ms);

	t1 =	rtw_get_current_time();
	iol_mode_enable(adapter, 1);
	for (i = 0; i < bndy_cnt; i++) {
		u8 page_no = 0;
		page_no = i * 2 ;
		ret = iol_ioconfig(adapter, page_no);
		if (ret != _SUCCESS)
			break;
	}
	iol_mode_enable(adapter, 0);
	t2 = rtw_get_current_time();
exit:
	/* restore BCN_HEAD */
	rtw_write8(adapter, REG_TDECTRL + 1, 0);
	return ret;
}

void rtw_IOL_cmd_tx_pkt_buf_dump(ADAPTER *Adapter, int data_len)
{
	u32 fifo_data, reg_140;
	u32 addr, rstatus, loop = 0;

	u16 data_cnts = (data_len / 8) + 1;
	u8 *pbuf = rtw_zvmalloc(data_len + 10);
	RTW_INFO("###### %s ######\n", __func__);

	rtw_write8(Adapter, REG_PKT_BUFF_ACCESS_CTRL, TXPKT_BUF_SELECT);
	if (pbuf) {
		for (addr = 0; addr < data_cnts; addr++) {
			rtw_write32(Adapter, 0x140, addr);
			rtw_usleep_os(2);
			loop = 0;
			do {
				rstatus = (reg_140 = rtw_read32(Adapter, REG_PKTBUF_DBG_CTRL) & BIT24);
				if (rstatus) {
					fifo_data = rtw_read32(Adapter, REG_PKTBUF_DBG_DATA_L);
					_rtw_memcpy(pbuf + (addr * 8), &fifo_data , 4);

					fifo_data = rtw_read32(Adapter, REG_PKTBUF_DBG_DATA_H);
					_rtw_memcpy(pbuf + (addr * 8 + 4), &fifo_data, 4);

				}
				rtw_usleep_os(2);
			} while (!rstatus && (loop++ < 10));
		}
		rtw_IOL_cmd_buf_dump(Adapter, data_len, pbuf);
		rtw_vmfree(pbuf, data_len + 10);

	}
	RTW_INFO("###### %s ######\n", __func__);
}

#endif /* defined(CONFIG_IOL) */


static void
_FWDownloadEnable_8188E(
	PADAPTER		padapter,
	bool			enable
)
{
	u8	tmp;

	if (enable) {
		/* MCU firmware download enable. */
		tmp = rtw_read8(padapter, REG_MCUFWDL);
		rtw_write8(padapter, REG_MCUFWDL, tmp | 0x01);

		/* 8051 reset */
		tmp = rtw_read8(padapter, REG_MCUFWDL + 2);
		rtw_write8(padapter, REG_MCUFWDL + 2, tmp & 0xf7);
	} else {

		/* MCU firmware download disable. */
		tmp = rtw_read8(padapter, REG_MCUFWDL);
		rtw_write8(padapter, REG_MCUFWDL, tmp & 0xfe);

		/* Reserved for fw extension. */
		rtw_write8(padapter, REG_MCUFWDL + 1, 0x00);
	}
}
#define MAX_REG_BOLCK_SIZE	196
static int
_BlockWrite(
		PADAPTER		padapter,
		void *		buffer,
		u32			buffSize
)
{
	int ret = _SUCCESS;

	u32			blockSize_p1 = 4;	/* (Default) Phase #1 : PCI muse use 4-byte write to download FW */
	u32			blockSize_p2 = 8;	/* Phase #2 : Use 8-byte, if Phase#1 use big size to write FW. */
	u32			blockSize_p3 = 1;	/* Phase #3 : Use 1-byte, the remnant of FW image. */
	u32			blockCount_p1 = 0, blockCount_p2 = 0, blockCount_p3 = 0;
	u32			remainSize_p1 = 0, remainSize_p2 = 0;
	u8			*bufferPtr	= (u8 *)buffer;
	u32			i = 0, offset = 0;

	blockSize_p1 = MAX_REG_BOLCK_SIZE;

	/* 3 Phase #1 */
	blockCount_p1 = buffSize / blockSize_p1;
	remainSize_p1 = buffSize % blockSize_p1;



	for (i = 0; i < blockCount_p1; i++) {
		ret = rtw_writeN(padapter, (FW_8188E_START_ADDRESS + i * blockSize_p1), blockSize_p1, (bufferPtr + i * blockSize_p1));

		if (ret == _FAIL)
			goto exit;
	}

	/* 3 Phase #2 */
	if (remainSize_p1) {
		offset = blockCount_p1 * blockSize_p1;

		blockCount_p2 = remainSize_p1 / blockSize_p2;
		remainSize_p2 = remainSize_p1 % blockSize_p2;



		for (i = 0; i < blockCount_p2; i++) {
			ret = rtw_writeN(padapter, (FW_8188E_START_ADDRESS + offset + i * blockSize_p2), blockSize_p2, (bufferPtr + offset + i * blockSize_p2));

			if (ret == _FAIL)
				goto exit;
		}
	}

	/* 3 Phase #3 */
	if (remainSize_p2) {
		offset = (blockCount_p1 * blockSize_p1) + (blockCount_p2 * blockSize_p2);

		blockCount_p3 = remainSize_p2 / blockSize_p3;


		for (i = 0 ; i < blockCount_p3 ; i++) {
			ret = rtw_write8(padapter, (FW_8188E_START_ADDRESS + offset + i), *(bufferPtr + offset + i));

			if (ret == _FAIL)
				goto exit;
		}
	}

exit:
	return ret;
}

static int
_PageWrite(
		PADAPTER	padapter,
		u32			page,
		void *		buffer,
		u32			size
)
{
	u8 value8;
	u8 u8Page = (u8)(page & 0x07) ;

	value8 = (rtw_read8(padapter, REG_MCUFWDL + 2) & 0xF8) | u8Page ;
	rtw_write8(padapter, REG_MCUFWDL + 2, value8);

	return _BlockWrite(padapter, buffer, size);
}

static void
_FillDummy(
	u8		*pFwBuf,
	u32	*pFwLen
)
{
	u32	FwLen = *pFwLen;
	u8	remain = (u8)(FwLen % 4);
	remain = (remain == 0) ? 0 : (4 - remain);

	while (remain > 0) {
		pFwBuf[FwLen] = 0;
		FwLen++;
		remain--;
	}

	*pFwLen = FwLen;
}

static int
_WriteFW(
		PADAPTER		padapter,
		void *			buffer,
		u32			size
)
{
	/* Since we need dynamic decide method of dwonload fw, so we call this function to get chip version. */
	int ret = _SUCCESS;
	u32	pageNums, remainSize ;
	u32	page, offset;
	u8		*bufferPtr = (u8 *)buffer;

	pageNums = size / MAX_DLFW_PAGE_SIZE ;
	/* RT_ASSERT((pageNums <= 4), ("Page numbers should not greater then 4\n")); */
	remainSize = size % MAX_DLFW_PAGE_SIZE;

	for (page = 0; page < pageNums; page++) {
		offset = page * MAX_DLFW_PAGE_SIZE;
		ret = _PageWrite(padapter, page, bufferPtr + offset, MAX_DLFW_PAGE_SIZE);

		if (ret == _FAIL)
			goto exit;
	}
	if (remainSize) {
		offset = pageNums * MAX_DLFW_PAGE_SIZE;
		page = pageNums;
		ret = _PageWrite(padapter, page, bufferPtr + offset, remainSize);

		if (ret == _FAIL)
			goto exit;

	}

exit:
	return ret;
}

static void _MCUIO_Reset88E(PADAPTER padapter, u8 bReset)
{
	u8 u1bTmp;

	if (bReset == true) {
		u1bTmp = rtw_read8(padapter, REG_RSV_CTRL);
		rtw_write8(padapter, REG_RSV_CTRL, (u1bTmp & (~BIT1)));
		/* Reset MCU IO Wrapper- sugggest by SD1-Gimmy */
		u1bTmp = rtw_read8(padapter, REG_RSV_CTRL + 1);
		rtw_write8(padapter, REG_RSV_CTRL + 1, (u1bTmp & (~BIT3)));
	} else {
		u1bTmp = rtw_read8(padapter, REG_RSV_CTRL);
		rtw_write8(padapter, REG_RSV_CTRL, (u1bTmp & (~BIT1)));
		/* Enable MCU IO Wrapper */
		u1bTmp = rtw_read8(padapter, REG_RSV_CTRL + 1);
		rtw_write8(padapter, REG_RSV_CTRL + 1, u1bTmp | BIT3);
	}

}

void _8051Reset88E(PADAPTER padapter)
{
	u8 u1bTmp;

	_MCUIO_Reset88E(padapter, true);
	u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN + 1);
	rtw_write8(padapter, REG_SYS_FUNC_EN + 1, u1bTmp & (~BIT2));
	_MCUIO_Reset88E(padapter, false);
	rtw_write8(padapter, REG_SYS_FUNC_EN + 1, u1bTmp | (BIT2));

	RTW_INFO("=====> _8051Reset88E(): 8051 reset success .\n");
}

static s32 polling_fwdl_chksum(_adapter *adapter, u32 min_cnt, u32 timeout_ms)
{
	s32 ret = _FAIL;
	u32 value32;
	u32 start = rtw_get_current_time();
	u32 cnt = 0;

	/* polling CheckSum report */
	do {
		cnt++;
		value32 = rtw_read32(adapter, REG_MCUFWDL);
		if (value32 & FWDL_ChkSum_rpt || RTW_CANNOT_IO(adapter))
			break;
		rtw_yield_os();
	} while (rtw_get_passing_time_ms(start) < timeout_ms || cnt < min_cnt);

	if (!(value32 & FWDL_ChkSum_rpt))
		goto exit;

	if (rtw_fwdl_test_trigger_chksum_fail())
		goto exit;

	ret = _SUCCESS;

exit:
	RTW_INFO("%s: Checksum report %s! (%u, %dms), REG_MCUFWDL:0x%08x\n", __func__
		, (ret == _SUCCESS) ? "OK" : "Fail", cnt, rtw_get_passing_time_ms(start), value32);

	return ret;
}

static s32 _FWFreeToGo(_adapter *adapter, u32 min_cnt, u32 timeout_ms)
{
	s32 ret = _FAIL;
	u32	value32;
	u32 start = rtw_get_current_time();
	u32 cnt = 0;

	value32 = rtw_read32(adapter, REG_MCUFWDL);
	value32 |= MCUFWDL_RDY;
	value32 &= ~WINTINI_RDY;
	rtw_write32(adapter, REG_MCUFWDL, value32);

	_8051Reset88E(adapter);

	/*  polling for FW ready */
	do {
		cnt++;
		value32 = rtw_read32(adapter, REG_MCUFWDL);
		if (value32 & WINTINI_RDY || RTW_CANNOT_IO(adapter))
			break;
		rtw_yield_os();
	} while (rtw_get_passing_time_ms(start) < timeout_ms || cnt < min_cnt);

	if (!(value32 & WINTINI_RDY))
		goto exit;

	if (rtw_fwdl_test_trigger_wintint_rdy_fail())
		goto exit;

	ret = _SUCCESS;

exit:
	RTW_INFO("%s: Polling FW ready %s! (%u, %dms), REG_MCUFWDL:0x%08x\n", __func__
		, (ret == _SUCCESS) ? "OK" : "Fail", cnt, rtw_get_passing_time_ms(start), value32);
	return ret;
}

#define IS_FW_81xxC(padapter)	(((GET_HAL_DATA(padapter))->FirmwareSignature & 0xFFF0) == 0x88C0)


#ifdef CONFIG_FILE_FWIMG
	extern char *rtw_fw_file_path;
	extern char *rtw_fw_wow_file_path;
	u8	FwBuffer8188E[FW_8188E_SIZE];
#endif /* CONFIG_FILE_FWIMG */

/*
 *	Description:
 *		Download 8192C firmware code.
 *
 *   */
s32 rtl8188e_FirmwareDownload(PADAPTER padapter, bool  bUsedWoWLANFw)
{
	s32	rtStatus = _SUCCESS;
	u8 write_fw = 0;
	u32 fwdl_start_time;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	PRT_FIRMWARE_8188E	pFirmware = NULL;
	PRT_8188E_FIRMWARE_HDR		pFwHdr = NULL;

	u8			*pFirmwareBuf;
	u32			FirmwareLen, tmp_fw_len = 0;
#ifdef CONFIG_FILE_FWIMG
	u8 *fwfilepath;
#endif /* CONFIG_FILE_FWIMG */

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
#endif

	pFirmware = (PRT_FIRMWARE_8188E)rtw_zmalloc(sizeof(RT_FIRMWARE_8188E));
	if (!pFirmware) {
		rtStatus = _FAIL;
		goto exit;
	}



#ifdef CONFIG_FILE_FWIMG
#ifdef CONFIG_WOWLAN
	if (bUsedWoWLANFw)
		fwfilepath = rtw_fw_wow_file_path;
	else
#endif /* CONFIG_WOWLAN */
	{
		fwfilepath = rtw_fw_file_path;
	}
#endif /* CONFIG_FILE_FWIMG */

#ifdef CONFIG_FILE_FWIMG
	if (rtw_is_file_readable(fwfilepath) == true) {
		RTW_INFO("%s accquire FW from file:%s\n", __func__, fwfilepath);
		pFirmware->eFWSource = FW_SOURCE_IMG_FILE;
	} else
#endif /* CONFIG_FILE_FWIMG */
	{
		pFirmware->eFWSource = FW_SOURCE_HEADER_FILE;
	}

	switch (pFirmware->eFWSource) {
	case FW_SOURCE_IMG_FILE:
#ifdef CONFIG_FILE_FWIMG
		rtStatus = rtw_retrieve_from_file(fwfilepath, FwBuffer8188E, FW_8188E_SIZE);
		pFirmware->ulFwLength = rtStatus >= 0 ? rtStatus : 0;
		pFirmware->szFwBuffer = FwBuffer8188E;
#endif /* CONFIG_FILE_FWIMG */
		break;
	case FW_SOURCE_HEADER_FILE:
		if (bUsedWoWLANFw) {
#ifdef CONFIG_WOWLAN
			if (pwrpriv->wowlan_mode) {
	#ifdef CONFIG_SFW_SUPPORTED
				if (IS_VENDOR_8188E_I_CUT_SERIES(padapter)) {
					pFirmware->szFwBuffer = array_mp_8188e_s_fw_wowlan;
					pFirmware->ulFwLength = array_length_mp_8188e_s_fw_wowlan;
				} else
	#endif
				{
					pFirmware->szFwBuffer = array_mp_8188e_t_fw_wowlan;
					pFirmware->ulFwLength = array_length_mp_8188e_t_fw_wowlan;
				}
				RTW_INFO("%s fw:%s, size: %d\n", __func__,
						"WoWLAN", pFirmware->ulFwLength);
			}
#endif /*CONFIG_WOWLAN*/

#ifdef CONFIG_AP_WOWLAN
			if (pwrpriv->wowlan_ap_mode) {
					pFirmware->szFwBuffer = array_mp_8188e_t_fw_ap;
					pFirmware->ulFwLength = array_length_mp_8188e_t_fw_ap;

					RTW_INFO("%s fw: %s, size: %d\n", __func__,
						"AP_WoWLAN", pFirmware->ulFwLength);
			}
#endif /*CONFIG_AP_WOWLAN*/
		} else {
#ifdef CONFIG_SFW_SUPPORTED
			if (IS_VENDOR_8188E_I_CUT_SERIES(padapter)) {
				pFirmware->szFwBuffer = array_mp_8188e_s_fw_nic;
				pFirmware->ulFwLength = array_length_mp_8188e_s_fw_nic;
			} else
#endif
			{
				pFirmware->szFwBuffer = array_mp_8188e_t_fw_nic;
				pFirmware->ulFwLength = array_length_mp_8188e_t_fw_nic;
			}
			RTW_INFO("%s fw:%s, size: %d\n", __func__, "NIC", pFirmware->ulFwLength);
		}
		break;
	}

	tmp_fw_len = IS_VENDOR_8188E_I_CUT_SERIES(padapter) ? FW_8188E_SIZE_2 : FW_8188E_SIZE;

	if ((pFirmware->ulFwLength - 32) > tmp_fw_len) {
		rtStatus = _FAIL;
		RTW_ERR("Firmware size:%u exceed %u\n", pFirmware->ulFwLength, tmp_fw_len);
		goto exit;
	}

	pFirmwareBuf = pFirmware->szFwBuffer;
	FirmwareLen = pFirmware->ulFwLength;

	/* To Check Fw header. Added by tynli. 2009.12.04. */
	pFwHdr = (PRT_8188E_FIRMWARE_HDR)pFirmwareBuf;

	pHalData->firmware_version =  le16_to_cpu(pFwHdr->Version);
	pHalData->firmware_sub_version = pFwHdr->Subversion;
	pHalData->FirmwareSignature = le16_to_cpu(pFwHdr->Signature);

	RTW_INFO("%s: fw_ver=%x fw_subver=%04x sig=0x%x, Month=%02x, Date=%02x, Hour=%02x, Minute=%02x\n",
		__func__, pHalData->firmware_version,
		pHalData->firmware_sub_version, pHalData->FirmwareSignature,
		pFwHdr->Month, pFwHdr->Date, pFwHdr->Hour, pFwHdr->Minute);

	if (IS_FW_HEADER_EXIST_88E(pFwHdr)) {
		/* Shift 32 bytes for FW header */
		pFirmwareBuf = pFirmwareBuf + 32;
		FirmwareLen = FirmwareLen - 32;
	}

	/* Suggested by Filen. If 8051 is running in RAM code, driver should inform Fw to reset by itself, */
	/* or it will cause download Fw fail. 2010.02.01. by tynli. */
	if (rtw_read8(padapter, REG_MCUFWDL) & RAM_DL_SEL) { /* 8051 RAM code */
		rtw_write8(padapter, REG_MCUFWDL, 0x00);
		_8051Reset88E(padapter);
	}

	_FWDownloadEnable_8188E(padapter, true);
	fwdl_start_time = rtw_get_current_time();
	while (!RTW_CANNOT_IO(padapter)
	       && (write_fw++ < 3 || rtw_get_passing_time_ms(fwdl_start_time) < 500)) {
		/* reset FWDL chksum */
		rtw_write8(padapter, REG_MCUFWDL, rtw_read8(padapter, REG_MCUFWDL) | FWDL_ChkSum_rpt);

		rtStatus = _WriteFW(padapter, pFirmwareBuf, FirmwareLen);
		if (rtStatus != _SUCCESS)
			continue;

		rtStatus = polling_fwdl_chksum(padapter, 5, 50);
		if (rtStatus == _SUCCESS)
			break;
	}
	_FWDownloadEnable_8188E(padapter, false);
	if (_SUCCESS != rtStatus)
		goto fwdl_stat;

	rtStatus = _FWFreeToGo(padapter, 10, 200);
	if (_SUCCESS != rtStatus)
		goto fwdl_stat;

fwdl_stat:
	RTW_INFO("FWDL %s. write_fw:%u, %dms\n"
		 , (rtStatus == _SUCCESS) ? "success" : "fail"
		 , write_fw
		 , rtw_get_passing_time_ms(fwdl_start_time)
		);

exit:
	if (pFirmware)
		rtw_mfree((u8 *)pFirmware, sizeof(RT_FIRMWARE_8188E));

	rtl8188e_InitializeFirmwareVars(padapter);

	return rtStatus;
}

void rtl8188e_InitializeFirmwareVars(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);

	/* Init Fw LPS related. */
	pwrpriv->bFwCurrentInPSMode = false;

	/* Init H2C cmd. */
	rtw_write8(padapter, REG_HMETFR, 0x0f);

	/* Init H2C counter. by tynli. 2009.12.09. */
	pHalData->LastHMEBoxNum = 0;
}

/* ***********************************************************
 *				Efuse related code
 * *********************************************************** */
enum {
	VOLTAGE_V25						= 0x03,
	LDOE25_SHIFT						= 28 ,
};

static bool
hal_EfusePgPacketWrite2ByteHeader(
	PADAPTER		pAdapter,
	u8				efuseType,
	u16				*pAddr,
	PPGPKT_STRUCT	pTargetPkt,
	bool			bPseudoTest);
static bool
hal_EfusePgPacketWrite1ByteHeader(
	PADAPTER		pAdapter,
	u8				efuseType,
	u16				*pAddr,
	PPGPKT_STRUCT	pTargetPkt,
	bool			bPseudoTest);
static bool
hal_EfusePgPacketWriteData(
	PADAPTER		pAdapter,
	u8				efuseType,
	u16				*pAddr,
	PPGPKT_STRUCT	pTargetPkt,
	bool			bPseudoTest);

static void
hal_EfusePowerSwitch_RTL8188E(
	PADAPTER	pAdapter,
	u8		bWrite,
	u8		PwrState)
{
	u8	tempval;
	u16	tmpV16;

	if (PwrState == true) {
		rtw_write8(pAdapter, REG_EFUSE_ACCESS, EFUSE_ACCESS_ON);
		/* Reset: 0x0000h[28], default valid */
		tmpV16 =  rtw_read16(pAdapter, REG_SYS_FUNC_EN);
		if (!(tmpV16 & FEN_ELDR)) {
			tmpV16 |= FEN_ELDR ;
			rtw_write16(pAdapter, REG_SYS_FUNC_EN, tmpV16);
		}

		/* Clock: Gated(0x0008h[5]) 8M(0x0008h[1]) clock from ANA, default valid */
		tmpV16 = rtw_read16(pAdapter, REG_SYS_CLKR);
		if ((!(tmpV16 & LOADER_CLK_EN))  || (!(tmpV16 & ANA8M))) {
			tmpV16 |= (LOADER_CLK_EN | ANA8M) ;
			rtw_write16(pAdapter, REG_SYS_CLKR, tmpV16);
		}

		if (bWrite == true) {
			/* Enable LDO 2.5V before read/write action */
			tempval = rtw_read8(pAdapter, EFUSE_TEST + 3);
			if (IS_VENDOR_8188E_I_CUT_SERIES(pAdapter)) {
				tempval &= 0x87;
				tempval |= 0x38; /* 0x34[30:27] = 0b'0111,  Use LDO 2.25V, Suggested by SD1 Pisa */
			} else {
				tempval &= 0x0F;
				tempval |= (VOLTAGE_V25 << 4);
			}
			rtw_write8(pAdapter, EFUSE_TEST + 3, (tempval | 0x80));
		}
	} else {
		rtw_write8(pAdapter, REG_EFUSE_ACCESS, EFUSE_ACCESS_OFF);

		if (bWrite == true) {
			/* Disable LDO 2.5V after read/write action */
			tempval = rtw_read8(pAdapter, EFUSE_TEST + 3);
			rtw_write8(pAdapter, EFUSE_TEST + 3, (tempval & 0x7F));
		}
	}
}

static void
rtl8188e_EfusePowerSwitch(
	PADAPTER	pAdapter,
	u8		bWrite,
	u8		PwrState)
{
	hal_EfusePowerSwitch_RTL8188E(pAdapter, bWrite, PwrState);
}



static bool efuse_read_phymap(
	PADAPTER	Adapter,
	u8			*pbuf,	/* buffer to store efuse physical map */
	u16			*size	/* the max byte to read. will update to byte read */
)
{
	u8 *pos = pbuf;
	u16 limit = *size;
	u16 addr = 0;
	bool reach_end = false;

	/*  */
	/* Refresh efuse init map as all 0xFF. */
	/*  */
	memset(pbuf, 0xFF, limit);


	/*  */
	/* Read physical efuse content. */
	/*  */
	while (addr < limit) {
		ReadEFuseByte(Adapter, addr, pos, false);
		if (*pos != 0xFF) {
			pos++;
			addr++;
		} else {
			reach_end = true;
			break;
		}
	}

	*size = addr;

	return reach_end;

}

static void
Hal_EfuseReadEFuse88E(
	PADAPTER		Adapter,
	u16			_offset,
	u16			_size_byte,
	u8		*pbuf,
	bool	bPseudoTest
)
{
	/* u8	efuseTbl[EFUSE_MAP_LEN_88E]; */
	u8	*efuseTbl = NULL;
	u8	rtemp8[1];
	u16	eFuse_Addr = 0;
	u8	offset, wren;
	u16	i, j;
	/* u16	eFuseWord[EFUSE_MAX_SECTION_88E][EFUSE_MAX_WORD_UNIT]; */
	u16	**eFuseWord = NULL;
	u16	efuse_utilized = 0;
	u8	efuse_usage = 0;
	u8	u1temp = 0;

	/*  */
	/* Do NOT excess total size of EFuse table. Added by Roger, 2008.11.10. */
	/*  */
	if ((_offset + _size_byte) > EFUSE_MAP_LEN_88E) {
		/* total E-Fuse table is 512bytes */
		RTW_INFO("Hal_EfuseReadEFuse88E(): Invalid offset(%#x) with read bytes(%#x)!!\n", _offset, _size_byte);
		goto exit;
	}

	efuseTbl = (u8 *)rtw_zmalloc(EFUSE_MAP_LEN_88E);
	if (efuseTbl == NULL) {
		RTW_INFO("%s: alloc efuseTbl fail!\n", __func__);
		goto exit;
	}

	eFuseWord = (u16 **)rtw_malloc2d(EFUSE_MAX_SECTION_88E, EFUSE_MAX_WORD_UNIT, 2);
	if (eFuseWord == NULL) {
		RTW_INFO("%s: alloc eFuseWord fail!\n", __func__);
		goto exit;
	}

	/* 0. Refresh efuse init map as all oxFF. */
	for (i = 0; i < EFUSE_MAX_SECTION_88E; i++)
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++)
			eFuseWord[i][j] = 0xFFFF;

	/*  */
	/* 1. Read the first byte to check if efuse is empty!!! */
	/*  */
	/*  */
	ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);
	if (*rtemp8 != 0xFF) {
		efuse_utilized++;
		/* RTW_INFO("efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, *rtemp8); */
		eFuse_Addr++;
	} else {
		RTW_INFO("EFUSE is empty efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, *rtemp8);
		goto exit;
	}


	/*  */
	/* 2. Read real efuse content. Filter PG header and every section data. */
	/*  */
	while ((*rtemp8 != 0xFF) && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E)) {
		/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("efuse_Addr-%d efuse_data=%x\n", eFuse_Addr-1, *rtemp8)); */

		/* Check PG header for section num. */
		if ((*rtemp8 & 0x1F) == 0x0F) {	/* extended header */
			u1temp = ((*rtemp8 & 0xE0) >> 5);
			/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header u1temp=%x *rtemp&0xE0 0x%x\n", u1temp, *rtemp8 & 0xE0)); */

			/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header u1temp=%x\n", u1temp)); */

			ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);

			/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, *rtemp8));	 */

			if ((*rtemp8 & 0x0F) == 0x0F) {
				eFuse_Addr++;
				ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);

				if (*rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E))
					eFuse_Addr++;
				continue;
			} else {
				offset = ((*rtemp8 & 0xF0) >> 1) | u1temp;
				wren = (*rtemp8 & 0x0F);
				eFuse_Addr++;
			}
		} else {
			offset = ((*rtemp8 >> 4) & 0x0f);
			wren = (*rtemp8 & 0x0f);
		}

		if (offset < EFUSE_MAX_SECTION_88E) {
			/* Get word enable value from PG header */
			/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Offset-%d Worden=%x\n", offset, wren)); */

			for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++) {
				/* Check word enable condition in the section				 */
				if (!(wren & 0x01)) {
					/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d\n", eFuse_Addr)); */
					ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);
					eFuse_Addr++;
					/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Data=0x%x\n", *rtemp8)); 				 */
					efuse_utilized++;
					eFuseWord[offset][i] = (*rtemp8 & 0xff);


					if (eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E)
						break;

					/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d", eFuse_Addr)); */
					ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);
					eFuse_Addr++;
					/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Data=0x%x\n", *rtemp8)); 				 */

					efuse_utilized++;
					eFuseWord[offset][i] |= (((u16)*rtemp8 << 8) & 0xff00);

					if (eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E)
						break;
				}

				wren >>= 1;

			}
		} else { /* deal with error offset,skip error data		 */
			RTW_PRINT("invalid offset:0x%02x\n", offset);
			for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++) {
				/* Check word enable condition in the section				 */
				if (!(wren & 0x01)) {
					eFuse_Addr++;
					efuse_utilized++;
					if (eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E)
						break;
					eFuse_Addr++;
					efuse_utilized++;
					if (eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E)
						break;
				}
			}
		}
		/* Read next PG header */
		ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);
		/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d rtemp 0x%x\n", eFuse_Addr, *rtemp8)); */

		if (*rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E)) {
			efuse_utilized++;
			eFuse_Addr++;
		}
	}

	/*  */
	/* 3. Collect 16 sections and 4 word unit into Efuse map. */
	/*  */
	for (i = 0; i < EFUSE_MAX_SECTION_88E; i++) {
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++) {
			efuseTbl[(i * 8) + (j * 2)] = (eFuseWord[i][j] & 0xff);
			efuseTbl[(i * 8) + ((j * 2) + 1)] = ((eFuseWord[i][j] >> 8) & 0xff);
		}
	}


	/*  */
	/* 4. Copy from Efuse map to output pointer memory!!! */
	/*  */
	for (i = 0; i < _size_byte; i++)
		pbuf[i] = efuseTbl[_offset + i];

	/*  */
	/* 5. Calculate Efuse utilization. */
	/*  */
	efuse_usage = (u8)((eFuse_Addr * 100) / EFUSE_REAL_CONTENT_LEN_88E);
	rtw_hal_set_hwreg(Adapter, HW_VAR_EFUSE_BYTES, (u8 *)&eFuse_Addr);

exit:
	if (efuseTbl)
		rtw_mfree(efuseTbl, EFUSE_MAP_LEN_88E);

	if (eFuseWord)
		rtw_mfree2d((void *)eFuseWord, EFUSE_MAX_SECTION_88E, EFUSE_MAX_WORD_UNIT, sizeof(u16));
}


static bool
Hal_EfuseSwitchToBank(
		PADAPTER	pAdapter,
		u8			bank,
		bool		bPseudoTest
)
{
	bool		bRet = false;
	u32		value32 = 0;

	/* RTPRINT(FEEPROM, EFUSE_PG, ("Efuse switch bank to %d\n", bank)); */
	if (bPseudoTest) {
		fakeEfuseBank = bank;
		bRet = true;
	} else
		bRet = true;
	return bRet;
}



static void
ReadEFuseByIC(
	PADAPTER	Adapter,
	u8		efuseType,
	u16		 _offset,
	u16		_size_byte,
	u8	*pbuf,
	bool	bPseudoTest
)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(Adapter);
#ifdef DBG_IOL_READ_EFUSE_MAP
	u8 logical_map[512];
#endif

#ifdef CONFIG_IOL_READ_EFUSE_MAP
	if (!bPseudoTest) { /* && rtw_IOL_applied(Adapter)) */
		int ret = _FAIL;
		if (rtw_IOL_applied(Adapter)) {
			rtw_hal_power_on(Adapter);

			iol_mode_enable(Adapter, 1);
#ifdef DBG_IOL_READ_EFUSE_MAP
			iol_read_efuse(Adapter, 0, _offset, _size_byte, logical_map);
#else
			ret = iol_read_efuse(Adapter, 0, _offset, _size_byte, pbuf);
#endif
			iol_mode_enable(Adapter, 0);

			if (_SUCCESS == ret)
				goto exit;
		}
	}
#endif
	Hal_EfuseReadEFuse88E(Adapter, _offset, _size_byte, pbuf, bPseudoTest);

exit:

#ifdef DBG_IOL_READ_EFUSE_MAP
	if (!memcmp(logical_map, pHalData->efuse_eeprom_data, 0x130) == false) {
		int i;
		RTW_INFO("%s compare first 0x130 byte fail\n", __func__);
		for (i = 0; i < 512; i++) {
			if (i % 16 == 0)
				RTW_INFO("0x%03x: ", i);
			RTW_INFO("%02x ", logical_map[i]);
			if (i % 16 == 15)
				RTW_INFO("\n");
		}
		RTW_INFO("\n");
	}
#endif

	return;
}

static void
ReadEFuse_Pseudo(
	PADAPTER	Adapter,
	u8		efuseType,
	u16		 _offset,
	u16		_size_byte,
	u8	*pbuf,
	bool	bPseudoTest
)
{
	Hal_EfuseReadEFuse88E(Adapter, _offset, _size_byte, pbuf, bPseudoTest);
}

static void
rtl8188e_ReadEFuse(
	PADAPTER	Adapter,
	u8		efuseType,
	u16		_offset,
	u16		_size_byte,
	u8	*pbuf,
	bool	bPseudoTest
)
{
	if (bPseudoTest)
		ReadEFuse_Pseudo(Adapter, efuseType, _offset, _size_byte, pbuf, bPseudoTest);
	else
		ReadEFuseByIC(Adapter, efuseType, _offset, _size_byte, pbuf, bPseudoTest);
}

/* Do not support BT */
static void
Hal_EFUSEGetEfuseDefinition88E(
		PADAPTER	pAdapter,
		u8		efuseType,
		u8		type,
		void *		pOut
)
{
	switch (type) {
	case TYPE_EFUSE_MAX_SECTION: {
		u8	*pMax_section;
		pMax_section = (u8 *)pOut;
		*pMax_section = EFUSE_MAX_SECTION_88E;
	}
	break;
	case TYPE_EFUSE_REAL_CONTENT_LEN: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = EFUSE_REAL_CONTENT_LEN_88E;
	}
	break;
	case TYPE_EFUSE_CONTENT_LEN_BANK: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = EFUSE_REAL_CONTENT_LEN_88E;
	}
	break;
	case TYPE_AVAILABLE_EFUSE_BYTES_BANK: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN_88E-EFUSE_OOB_PROTECT_BYTES_88E);
	}
	break;
	case TYPE_AVAILABLE_EFUSE_BYTES_TOTAL: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN_88E-EFUSE_OOB_PROTECT_BYTES_88E);
	}
	break;
	case TYPE_EFUSE_MAP_LEN: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = (u16)EFUSE_MAP_LEN_88E;
	}
	break;
	case TYPE_EFUSE_PROTECT_BYTES_BANK: {
		u8 *pu1Tmp;
		pu1Tmp = (u8 *)pOut;
		*pu1Tmp = (u8)(EFUSE_OOB_PROTECT_BYTES_88E);
	}
	break;
	default: {
		u8 *pu1Tmp;
		pu1Tmp = (u8 *)pOut;
		*pu1Tmp = 0;
	}
	break;
	}
}

static void
Hal_EFUSEGetEfuseDefinition_Pseudo88E(
		PADAPTER	pAdapter,
		u8			efuseType,
		u8			type,
		void *		pOut
)
{
	switch (type) {
	case TYPE_EFUSE_MAX_SECTION: {
		u8		*pMax_section;
		pMax_section = (u8 *)pOut;
		*pMax_section = EFUSE_MAX_SECTION_88E;
	}
	break;
	case TYPE_EFUSE_REAL_CONTENT_LEN: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = EFUSE_REAL_CONTENT_LEN_88E;
	}
	break;
	case TYPE_EFUSE_CONTENT_LEN_BANK: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = EFUSE_REAL_CONTENT_LEN_88E;
	}
	break;
	case TYPE_AVAILABLE_EFUSE_BYTES_BANK: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN_88E-EFUSE_OOB_PROTECT_BYTES_88E);
	}
	break;
	case TYPE_AVAILABLE_EFUSE_BYTES_TOTAL: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN_88E-EFUSE_OOB_PROTECT_BYTES_88E);
	}
	break;
	case TYPE_EFUSE_MAP_LEN: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = (u16)EFUSE_MAP_LEN_88E;
	}
	break;
	case TYPE_EFUSE_PROTECT_BYTES_BANK: {
		u8 *pu1Tmp;
		pu1Tmp = (u8 *)pOut;
		*pu1Tmp = (u8)(EFUSE_OOB_PROTECT_BYTES_88E);
	}
	break;
	default: {
		u8 *pu1Tmp;
		pu1Tmp = (u8 *)pOut;
		*pu1Tmp = 0;
	}
	break;
	}
}


static void
rtl8188e_EFUSE_GetEfuseDefinition(
		PADAPTER	pAdapter,
		u8		efuseType,
		u8		type,
		void		*pOut,
		bool		bPseudoTest
)
{
	if (bPseudoTest)
		Hal_EFUSEGetEfuseDefinition_Pseudo88E(pAdapter, efuseType, type, pOut);
	else
		Hal_EFUSEGetEfuseDefinition88E(pAdapter, efuseType, type, pOut);
}

static u8
Hal_EfuseWordEnableDataWrite(PADAPTER	pAdapter,
			     u16		efuse_addr,
			     u8		word_en,
			     u8		*data,
			     bool		bPseudoTest)
{
	u16	tmpaddr = 0;
	u16	start_addr = efuse_addr;
	u8	badworden = 0x0F;
	u8	tmpdata[8];

	memset((void *)tmpdata, 0xff, PGPKT_DATA_SIZE);

	if (!(word_en & BIT0)) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(pAdapter, start_addr++, data[0], bPseudoTest);
		efuse_OneByteWrite(pAdapter, start_addr++, data[1], bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 0);

		efuse_OneByteRead(pAdapter, tmpaddr, &tmpdata[0], bPseudoTest);
		efuse_OneByteRead(pAdapter, tmpaddr + 1, &tmpdata[1], bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 1);

		if ((data[0] != tmpdata[0]) || (data[1] != tmpdata[1]))
			badworden &= (~BIT0);
	}
	if (!(word_en & BIT1)) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(pAdapter, start_addr++, data[2], bPseudoTest);
		efuse_OneByteWrite(pAdapter, start_addr++, data[3], bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 0);

		efuse_OneByteRead(pAdapter, tmpaddr    , &tmpdata[2], bPseudoTest);
		efuse_OneByteRead(pAdapter, tmpaddr + 1, &tmpdata[3], bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 1);

		if ((data[2] != tmpdata[2]) || (data[3] != tmpdata[3]))
			badworden &= (~BIT1);
	}
	if (!(word_en & BIT2)) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(pAdapter, start_addr++, data[4], bPseudoTest);
		efuse_OneByteWrite(pAdapter, start_addr++, data[5], bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 0);

		efuse_OneByteRead(pAdapter, tmpaddr, &tmpdata[4], bPseudoTest);
		efuse_OneByteRead(pAdapter, tmpaddr + 1, &tmpdata[5], bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 1);

		if ((data[4] != tmpdata[4]) || (data[5] != tmpdata[5]))
			badworden &= (~BIT2);
	}
	if (!(word_en & BIT3)) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(pAdapter, start_addr++, data[6], bPseudoTest);
		efuse_OneByteWrite(pAdapter, start_addr++, data[7], bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 0);

		efuse_OneByteRead(pAdapter, tmpaddr, &tmpdata[6], bPseudoTest);
		efuse_OneByteRead(pAdapter, tmpaddr + 1, &tmpdata[7], bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 1);

		if ((data[6] != tmpdata[6]) || (data[7] != tmpdata[7]))
			badworden &= (~BIT3);
	}
	return badworden;
}

static u8
Hal_EfuseWordEnableDataWrite_Pseudo(PADAPTER	pAdapter,
				    u16		efuse_addr,
				    u8		word_en,
				    u8		*data,
				    bool		bPseudoTest)
{
	u8	ret = 0;

	ret = Hal_EfuseWordEnableDataWrite(pAdapter, efuse_addr, word_en, data, bPseudoTest);

	return ret;
}

static u8
rtl8188e_Efuse_WordEnableDataWrite(PADAPTER	pAdapter,
				   u16		efuse_addr,
				   u8		word_en,
				   u8		*data,
				   bool		bPseudoTest)
{
	u8	ret = 0;

	if (bPseudoTest)
		ret = Hal_EfuseWordEnableDataWrite_Pseudo(pAdapter, efuse_addr, word_en, data, bPseudoTest);
	else
		ret = Hal_EfuseWordEnableDataWrite(pAdapter, efuse_addr, word_en, data, bPseudoTest);

	return ret;
}


static u16
hal_EfuseGetCurrentSize_8188e(PADAPTER	pAdapter,
			      	bool			bPseudoTest)
{
	int	bContinual = true;

	u16	efuse_addr = 0;
	u8	hoffset = 0, hworden = 0;
	u8	efuse_data, word_cnts = 0;

	if (bPseudoTest)
		efuse_addr = (u16)(fakeEfuseUsedBytes);
	else
		rtw_hal_get_hwreg(pAdapter, HW_VAR_EFUSE_BYTES, (u8 *)&efuse_addr);
	/* RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfuseGetCurrentSize_8723A(), start_efuse_addr = %d\n", efuse_addr)); */

	while (bContinual &&
	       efuse_OneByteRead(pAdapter, efuse_addr , &efuse_data, bPseudoTest) &&
	       AVAILABLE_EFUSE_ADDR(efuse_addr)) {
		if (efuse_data != 0xFF) {
			if ((efuse_data & 0x1F) == 0x0F) {	/* extended header */
				hoffset = efuse_data;
				efuse_addr++;
				efuse_OneByteRead(pAdapter, efuse_addr , &efuse_data, bPseudoTest);
				if ((efuse_data & 0x0F) == 0x0F) {
					efuse_addr++;
					continue;
				} else {
					hoffset = ((hoffset & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
					hworden = efuse_data & 0x0F;
				}
			} else {
				hoffset = (efuse_data >> 4) & 0x0F;
				hworden =  efuse_data & 0x0F;
			}
			word_cnts = Efuse_CalculateWordCnts(hworden);
			/* read next header */
			efuse_addr = efuse_addr + (word_cnts * 2) + 1;
		} else
			bContinual = false ;
	}

	if (bPseudoTest) {
		fakeEfuseUsedBytes = efuse_addr;
		/* RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfuseGetCurrentSize_8723A(), return %d\n", fakeEfuseUsedBytes)); */
	} else {
		rtw_hal_set_hwreg(pAdapter, HW_VAR_EFUSE_BYTES, (u8 *)&efuse_addr);
		/* RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfuseGetCurrentSize_8723A(), return %d\n", efuse_addr)); */
	}

	return efuse_addr;
}

static u16
Hal_EfuseGetCurrentSize_Pseudo(PADAPTER	pAdapter,
			       	bool			bPseudoTest)
{
	u16	ret = 0;

	ret = hal_EfuseGetCurrentSize_8188e(pAdapter, bPseudoTest);

	return ret;
}


static u16
rtl8188e_EfuseGetCurrentSize(
	PADAPTER	pAdapter,
	u8			efuseType,
	bool		bPseudoTest)
{
	u16	ret = 0;

	if (bPseudoTest)
		ret = Hal_EfuseGetCurrentSize_Pseudo(pAdapter, bPseudoTest);
	else
		ret = hal_EfuseGetCurrentSize_8188e(pAdapter, bPseudoTest);


	return ret;
}


static int
hal_EfusePgPacketRead_8188e(
	PADAPTER	pAdapter,
	u8			offset,
	u8			*data,
	bool		bPseudoTest)
{
	u8	ReadState = PG_STATE_HEADER;

	int	bContinual = true;
	int	bDataEmpty = true ;

	u8	efuse_data, word_cnts = 0;
	u16	efuse_addr = 0;
	u8	hoffset = 0, hworden = 0;
	u8	tmpidx = 0;
	u8	tmpdata[8];
	u8	max_section = 0;
	u8	tmp_header = 0;

	EFUSE_GetEfuseDefinition(pAdapter, EFUSE_WIFI, TYPE_EFUSE_MAX_SECTION, (void *)&max_section, bPseudoTest);

	if (data == NULL)
		return false;
	if (offset > max_section)
		return false;

	memset((void *)data, 0xff, sizeof(u8) * PGPKT_DATA_SIZE);
	memset((void *)tmpdata, 0xff, sizeof(u8) * PGPKT_DATA_SIZE);


	/*  */
	/* <Roger_TODO> Efuse has been pre-programmed dummy 5Bytes at the end of Efuse by CP. */
	/* Skip dummy parts to prevent unexpected data read from Efuse. */
	/* By pass right now. 2009.02.19. */
	/*  */
	while (bContinual && AVAILABLE_EFUSE_ADDR(efuse_addr)) {
		/* -------  Header Read ------------- */
		if (ReadState & PG_STATE_HEADER) {
			if (efuse_OneByteRead(pAdapter, efuse_addr , &efuse_data, bPseudoTest) && (efuse_data != 0xFF)) {
				if (EXT_HEADER(efuse_data)) {
					tmp_header = efuse_data;
					efuse_addr++;
					efuse_OneByteRead(pAdapter, efuse_addr , &efuse_data, bPseudoTest);
					if (!ALL_WORDS_DISABLED(efuse_data)) {
						hoffset = ((tmp_header & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
						hworden = efuse_data & 0x0F;
					} else {
						RTW_INFO("Error, All words disabled\n");
						efuse_addr++;
						continue;
					}
				} else {
					hoffset = (efuse_data >> 4) & 0x0F;
					hworden =  efuse_data & 0x0F;
				}
				word_cnts = Efuse_CalculateWordCnts(hworden);
				bDataEmpty = true ;

				if (hoffset == offset) {
					for (tmpidx = 0; tmpidx < word_cnts * 2 ; tmpidx++) {
						if (efuse_OneByteRead(pAdapter, efuse_addr + 1 + tmpidx , &efuse_data, bPseudoTest)) {
							tmpdata[tmpidx] = efuse_data;
							if (efuse_data != 0xff)
								bDataEmpty = false;
						}
					}
					if (bDataEmpty == false)
						ReadState = PG_STATE_DATA;
					else { /* read next header */
						efuse_addr = efuse_addr + (word_cnts * 2) + 1;
						ReadState = PG_STATE_HEADER;
					}
				} else { /* read next header */
					efuse_addr = efuse_addr + (word_cnts * 2) + 1;
					ReadState = PG_STATE_HEADER;
				}

			} else
				bContinual = false ;
		}
		/* -------  Data section Read ------------- */
		else if (ReadState & PG_STATE_DATA) {
			efuse_WordEnableDataRead(hworden, tmpdata, data);
			efuse_addr = efuse_addr + (word_cnts * 2) + 1;
			ReadState = PG_STATE_HEADER;
		}

	}

	if ((data[0] == 0xff) && (data[1] == 0xff) && (data[2] == 0xff)  && (data[3] == 0xff) &&
	    (data[4] == 0xff) && (data[5] == 0xff) && (data[6] == 0xff)  && (data[7] == 0xff))
		return false;
	else
		return true;

}

static int
Hal_EfusePgPacketRead(PADAPTER	pAdapter,
		      u8			offset,
		      u8			*data,
		      bool			bPseudoTest)
{
	int	ret = 0;

	ret = hal_EfusePgPacketRead_8188e(pAdapter, offset, data, bPseudoTest);


	return ret;
}

static int
Hal_EfusePgPacketRead_Pseudo(PADAPTER	pAdapter,
			     u8			offset,
			     u8			*data,
			     bool		bPseudoTest)
{
	int	ret = 0;

	ret = hal_EfusePgPacketRead_8188e(pAdapter, offset, data, bPseudoTest);

	return ret;
}

static int
rtl8188e_Efuse_PgPacketRead(PADAPTER	pAdapter,
			    u8			offset,
			    u8			*data,
			    bool		bPseudoTest)
{
	int	ret = 0;

	if (bPseudoTest)
		ret = Hal_EfusePgPacketRead_Pseudo(pAdapter, offset, data, bPseudoTest);
	else
		ret = Hal_EfusePgPacketRead(pAdapter, offset, data, bPseudoTest);

	return ret;
}

static bool
hal_EfuseFixHeaderProcess(
		PADAPTER			pAdapter,
		u8					efuseType,
		PPGPKT_STRUCT		pFixPkt,
		u16					*pAddr,
		bool				bPseudoTest
)
{
	u8	originaldata[8], badworden = 0;
	u16	efuse_addr = *pAddr;
	u32	PgWriteSuccess = 0;

	memset((void *)originaldata, 0xff, 8);

	if (Efuse_PgPacketRead(pAdapter, pFixPkt->offset, originaldata, bPseudoTest)) {
		/* check if data exist */
		badworden = Efuse_WordEnableDataWrite(pAdapter, efuse_addr + 1, pFixPkt->word_en, originaldata, bPseudoTest);

		if (badworden != 0xf) {	/* write fail */
			PgWriteSuccess = Efuse_PgPacketWrite(pAdapter, pFixPkt->offset, badworden, originaldata, bPseudoTest);

			if (!PgWriteSuccess)
				return false;
			else
				efuse_addr = Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest);
		} else
			efuse_addr = efuse_addr + (pFixPkt->word_cnts * 2) + 1;
	} else
		efuse_addr = efuse_addr + (pFixPkt->word_cnts * 2) + 1;
	*pAddr = efuse_addr;
	return true;
}

static bool
hal_EfusePgPacketWrite2ByteHeader(
			PADAPTER		pAdapter,
			u8				efuseType,
			u16				*pAddr,
			PPGPKT_STRUCT	pTargetPkt,
			bool			bPseudoTest)
{
	bool		bRet = false, bContinual = true;
	u16	efuse_addr = *pAddr, efuse_max_available_len = 0;
	u8	pg_header = 0, tmp_header = 0, pg_header_temp = 0;
	u8	repeatcnt = 0;

	/* RTPRINT(FEEPROM, EFUSE_PG, ("Wirte 2byte header\n")); */
	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_BANK, (void *)&efuse_max_available_len, bPseudoTest);

	while (efuse_addr < efuse_max_available_len) {
		pg_header = ((pTargetPkt->offset & 0x07) << 5) | 0x0F;
		/* RTPRINT(FEEPROM, EFUSE_PG, ("pg_header = 0x%x\n", pg_header)); */
		efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 0);
		efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 1);
		while (tmp_header == 0xFF || pg_header != tmp_header) {
			if (repeatcnt++ > EFUSE_REPEAT_THRESHOLD_) {
				/* RTPRINT(FEEPROM, EFUSE_PG, ("Repeat over limit for pg_header!!\n")); */
				return false;
			}

			efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
			efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);
		}

		/* to write ext_header */
		if (tmp_header == pg_header) {
			efuse_addr++;
			pg_header_temp = pg_header;
			pg_header = ((pTargetPkt->offset & 0x78) << 1) | pTargetPkt->word_en;

			efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
			phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 0);
			efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);
			phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 1);
			while (tmp_header == 0xFF || pg_header != tmp_header) {
				if (repeatcnt++ > EFUSE_REPEAT_THRESHOLD_) {
					/* RTPRINT(FEEPROM, EFUSE_PG, ("Repeat over limit for ext_header!!\n")); */
					return false;
				}

				efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
				efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);
			}

			if ((tmp_header & 0x0F) == 0x0F) {	/* word_en PG fail */
				if (repeatcnt++ > EFUSE_REPEAT_THRESHOLD_) {
					/* RTPRINT(FEEPROM, EFUSE_PG, ("Repeat over limit for word_en!!\n")); */
					return false;
				} else {
					efuse_addr++;
					continue;
				}
			} else if (pg_header != tmp_header) {	/* offset PG fail */
				PGPKT_STRUCT	fixPkt;
				/* RTPRINT(FEEPROM, EFUSE_PG, ("Error condition for offset PG fail, need to cover the existed data\n")); */
				fixPkt.offset = ((pg_header_temp & 0xE0) >> 5) | ((tmp_header & 0xF0) >> 1);
				fixPkt.word_en = tmp_header & 0x0F;
				fixPkt.word_cnts = Efuse_CalculateWordCnts(fixPkt.word_en);
				if (!hal_EfuseFixHeaderProcess(pAdapter, efuseType, &fixPkt, &efuse_addr, bPseudoTest))
					return false;
			} else {
				bRet = true;
				break;
			}
		} else if ((tmp_header & 0x1F) == 0x0F) {	/* wrong extended header */
			efuse_addr += 2;
			continue;
		}
	}

	*pAddr = efuse_addr;
	return bRet;
}

static bool
hal_EfusePgPacketWrite1ByteHeader(
			PADAPTER		pAdapter,
			u8				efuseType,
			u16				*pAddr,
			PPGPKT_STRUCT	pTargetPkt,
			bool			bPseudoTest)
{
	bool		bRet = false;
	u8	pg_header = 0, tmp_header = 0;
	u16	efuse_addr = *pAddr;
	u8	repeatcnt = 0;

	/* RTPRINT(FEEPROM, EFUSE_PG, ("Wirte 1byte header\n")); */
	pg_header = ((pTargetPkt->offset << 4) & 0xf0) | pTargetPkt->word_en;

	efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
	phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 0);

	efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);

	phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 1);

	while (tmp_header == 0xFF || pg_header != tmp_header) {
		if (repeatcnt++ > EFUSE_REPEAT_THRESHOLD_)
			return false;
		efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
		efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);
	}

	if (pg_header == tmp_header)
		bRet = true;
	else {
		PGPKT_STRUCT	fixPkt;
		/* RTPRINT(FEEPROM, EFUSE_PG, ("Error condition for fixed PG packet, need to cover the existed data\n")); */
		fixPkt.offset = (tmp_header >> 4) & 0x0F;
		fixPkt.word_en = tmp_header & 0x0F;
		fixPkt.word_cnts = Efuse_CalculateWordCnts(fixPkt.word_en);
		if (!hal_EfuseFixHeaderProcess(pAdapter, efuseType, &fixPkt, &efuse_addr, bPseudoTest))
			return false;
	}

	*pAddr = efuse_addr;
	return bRet;
}

static bool
hal_EfusePgPacketWriteData(
			PADAPTER		pAdapter,
			u8				efuseType,
			u16				*pAddr,
			PPGPKT_STRUCT	pTargetPkt,
			bool			bPseudoTest)
{
	bool	bRet = false;
	u16	efuse_addr = *pAddr;
	u8	badworden = 0;
	u32	PgWriteSuccess = 0;

	badworden = 0x0f;
	badworden = Efuse_WordEnableDataWrite(pAdapter, efuse_addr + 1, pTargetPkt->word_en, pTargetPkt->data, bPseudoTest);
	if (badworden == 0x0F) {
		/* write ok */
		/* RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfusePgPacketWriteData ok!!\n")); */
		return true;
	} else {
		/* RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfusePgPacketWriteData Fail!!\n")); */
		/* reorganize other pg packet */

		PgWriteSuccess = Efuse_PgPacketWrite(pAdapter, pTargetPkt->offset, badworden, pTargetPkt->data, bPseudoTest);

		if (!PgWriteSuccess)
			return false;
		else
			return true;
	}

	return bRet;
}

static bool
hal_EfusePgPacketWriteHeader(
			PADAPTER		pAdapter,
			u8				efuseType,
			u16				*pAddr,
			PPGPKT_STRUCT	pTargetPkt,
			bool			bPseudoTest)
{
	bool		bRet = false;

	if (pTargetPkt->offset >= EFUSE_MAX_SECTION_BASE)
		bRet = hal_EfusePgPacketWrite2ByteHeader(pAdapter, efuseType, pAddr, pTargetPkt, bPseudoTest);
	else
		bRet = hal_EfusePgPacketWrite1ByteHeader(pAdapter, efuseType, pAddr, pTargetPkt, bPseudoTest);

	return bRet;
}

static bool
wordEnMatched(
	PPGPKT_STRUCT	pTargetPkt,
	PPGPKT_STRUCT	pCurPkt,
	u8				*pWden
)
{
	u8	match_word_en = 0x0F;	/* default all words are disabled */
	u8	i;

	/* check if the same words are enabled both target and current PG packet */
	if (((pTargetPkt->word_en & BIT0) == 0) &&
	    ((pCurPkt->word_en & BIT0) == 0)) {
		match_word_en &= ~BIT0;				/* enable word 0 */
	}
	if (((pTargetPkt->word_en & BIT1) == 0) &&
	    ((pCurPkt->word_en & BIT1) == 0)) {
		match_word_en &= ~BIT1;				/* enable word 1 */
	}
	if (((pTargetPkt->word_en & BIT2) == 0) &&
	    ((pCurPkt->word_en & BIT2) == 0)) {
		match_word_en &= ~BIT2;				/* enable word 2 */
	}
	if (((pTargetPkt->word_en & BIT3) == 0) &&
	    ((pCurPkt->word_en & BIT3) == 0)) {
		match_word_en &= ~BIT3;				/* enable word 3 */
	}

	*pWden = match_word_en;

	if (match_word_en != 0xf)
		return true;
	else
		return false;
}

static bool
hal_EfuseCheckIfDatafollowed(
		PADAPTER		pAdapter,
		u8				word_cnts,
		u16				startAddr,
		bool			bPseudoTest
)
{
	bool		bRet = false;
	u8	i, efuse_data;

	for (i = 0; i < (word_cnts * 2) ; i++) {
		if (efuse_OneByteRead(pAdapter, (startAddr + i) , &efuse_data, bPseudoTest) && (efuse_data != 0xFF))
			bRet = true;
	}

	return bRet;
}

static bool
hal_EfusePartialWriteCheck(
	PADAPTER		pAdapter,
	u8				efuseType,
	u16				*pAddr,
	PPGPKT_STRUCT	pTargetPkt,
	bool			bPseudoTest
)
{
	bool		bRet = false;
	u8	i, efuse_data = 0, cur_header = 0;
	u8	new_wden = 0, matched_wden = 0, badworden = 0;
	u16	startAddr = 0, efuse_max_available_len = 0, efuse_max = 0;
	PGPKT_STRUCT	curPkt;

	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_BANK, (void *)&efuse_max_available_len, bPseudoTest);
	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_EFUSE_REAL_CONTENT_LEN, (void *)&efuse_max, bPseudoTest);

	if (efuseType == EFUSE_WIFI) {
		if (bPseudoTest)
			startAddr = (u16)(fakeEfuseUsedBytes % EFUSE_REAL_CONTENT_LEN);
		else {
			rtw_hal_get_hwreg(pAdapter, HW_VAR_EFUSE_BYTES, (u8 *)&startAddr);
			startAddr %= EFUSE_REAL_CONTENT_LEN;
		}
	} else {
		if (bPseudoTest)
			startAddr = (u16)(fakeBTEfuseUsedBytes % EFUSE_REAL_CONTENT_LEN);
		else
			startAddr = (u16)(BTEfuseUsedBytes % EFUSE_REAL_CONTENT_LEN);
	}
	/* RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfusePartialWriteCheck(), startAddr=%d\n", startAddr)); */

	while (1) {
		if (startAddr >= efuse_max_available_len) {
			bRet = false;
			break;
		}

		if (efuse_OneByteRead(pAdapter, startAddr, &efuse_data, bPseudoTest) && (efuse_data != 0xFF)) {
			if (EXT_HEADER(efuse_data)) {
				cur_header = efuse_data;
				startAddr++;
				efuse_OneByteRead(pAdapter, startAddr, &efuse_data, bPseudoTest);
				if (ALL_WORDS_DISABLED(efuse_data)) {
					/* RTPRINT(FEEPROM, EFUSE_PG, ("Error condition, all words disabled")); */
					bRet = false;
					break;
				} else {
					curPkt.offset = ((cur_header & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
					curPkt.word_en = efuse_data & 0x0F;
				}
			} else {
				cur_header  =  efuse_data;
				curPkt.offset = (cur_header >> 4) & 0x0F;
				curPkt.word_en = cur_header & 0x0F;
			}

			curPkt.word_cnts = Efuse_CalculateWordCnts(curPkt.word_en);
			/* if same header is found but no data followed */
			/* write some part of data followed by the header. */
			if ((curPkt.offset == pTargetPkt->offset) &&
			    (!hal_EfuseCheckIfDatafollowed(pAdapter, curPkt.word_cnts, startAddr + 1, bPseudoTest)) &&
			    wordEnMatched(pTargetPkt, &curPkt, &matched_wden)) {
				/* RTPRINT(FEEPROM, EFUSE_PG, ("Need to partial write data by the previous wrote header\n")); */
				/* Here to write partial data */
				badworden = Efuse_WordEnableDataWrite(pAdapter, startAddr + 1, matched_wden, pTargetPkt->data, bPseudoTest);
				if (badworden != 0x0F) {
					u32	PgWriteSuccess = 0;
					/* if write fail on some words, write these bad words again */

					PgWriteSuccess = Efuse_PgPacketWrite(pAdapter, pTargetPkt->offset, badworden, pTargetPkt->data, bPseudoTest);

					if (!PgWriteSuccess) {
						bRet = false;	/* write fail, return */
						break;
					}
				}
				/* partial write ok, update the target packet for later use */
				for (i = 0; i < 4; i++) {
					if ((matched_wden & (0x1 << i)) == 0) {	/* this word has been written */
						pTargetPkt->word_en |= (0x1 << i);	/* disable the word */
					}
				}
				pTargetPkt->word_cnts = Efuse_CalculateWordCnts(pTargetPkt->word_en);
			}
			/* read from next header */
			startAddr = startAddr + (curPkt.word_cnts * 2) + 1;
		} else {
			/* not used header, 0xff */
			*pAddr = startAddr;
			/* RTPRINT(FEEPROM, EFUSE_PG, ("Started from unused header offset=%d\n", startAddr)); */
			bRet = true;
			break;
		}
	}
	return bRet;
}

static bool
hal_EfusePgCheckAvailableAddr(
	PADAPTER	pAdapter,
	u8			efuseType,
	bool		bPseudoTest
)
{
	u16	efuse_max_available_len = 0;

	/* Change to check TYPE_EFUSE_MAP_LEN ,beacuse 8188E raw 256,logic map over 256. */
	EFUSE_GetEfuseDefinition(pAdapter, EFUSE_WIFI, TYPE_EFUSE_MAP_LEN, (void *)&efuse_max_available_len, false);

	/* EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&efuse_max_available_len, bPseudoTest); */
	/* RTPRINT(FEEPROM, EFUSE_PG, ("efuse_max_available_len = %d\n", efuse_max_available_len)); */

	if (Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest) >= efuse_max_available_len) {
		/* RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfusePgCheckAvailableAddr error!!\n")); */
		return false;
	}
	return true;
}

static void
hal_EfuseConstructPGPkt(
	u8				offset,
	u8				word_en,
	u8				*pData,
	PPGPKT_STRUCT	pTargetPkt

)
{
	memset((void *)pTargetPkt->data, 0xFF, sizeof(u8) * 8);
	pTargetPkt->offset = offset;
	pTargetPkt->word_en = word_en;
	efuse_WordEnableDataRead(word_en, pData, pTargetPkt->data);
	pTargetPkt->word_cnts = Efuse_CalculateWordCnts(pTargetPkt->word_en);

	/* RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfuseConstructPGPkt(), targetPkt, offset=%d, word_en=0x%x, word_cnts=%d\n", pTargetPkt->offset, pTargetPkt->word_en, pTargetPkt->word_cnts)); */
}

static bool
hal_EfusePgPacketWrite_BT(
	PADAPTER	pAdapter,
	u8			offset,
	u8			word_en,
	u8			*pData,
	bool		bPseudoTest
)
{
	PGPKT_STRUCT	targetPkt;
	u16	startAddr = 0;
	u8	efuseType = EFUSE_BT;

	if (!hal_EfusePgCheckAvailableAddr(pAdapter, efuseType, bPseudoTest))
		return false;

	hal_EfuseConstructPGPkt(offset, word_en, pData, &targetPkt);

	if (!hal_EfusePartialWriteCheck(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return false;

	if (!hal_EfusePgPacketWriteHeader(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return false;

	if (!hal_EfusePgPacketWriteData(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return false;

	return true;
}

static bool
hal_EfusePgPacketWrite_8188e(
	PADAPTER		pAdapter,
	u8			offset,
	u8			word_en,
	u8			*pData,
	bool		bPseudoTest
)
{
	PGPKT_STRUCT	targetPkt;
	u16			startAddr = 0;
	u8			efuseType = EFUSE_WIFI;

	if (!hal_EfusePgCheckAvailableAddr(pAdapter, efuseType, bPseudoTest))
		return false;

	hal_EfuseConstructPGPkt(offset, word_en, pData, &targetPkt);

	if (!hal_EfusePartialWriteCheck(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return false;

	if (!hal_EfusePgPacketWriteHeader(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return false;

	if (!hal_EfusePgPacketWriteData(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return false;

	return true;
}


static int
Hal_EfusePgPacketWrite_Pseudo(PADAPTER	pAdapter,
			      u8			offset,
			      u8			word_en,
			      u8			*data,
			      bool		bPseudoTest)
{
	int ret;

	ret = hal_EfusePgPacketWrite_8188e(pAdapter, offset, word_en, data, bPseudoTest);

	return ret;
}

static int
Hal_EfusePgPacketWrite(PADAPTER	pAdapter,
		       u8			offset,
		       u8			word_en,
		       u8			*data,
		       bool		bPseudoTest)
{
	int	ret = 0;
	ret = hal_EfusePgPacketWrite_8188e(pAdapter, offset, word_en, data, bPseudoTest);


	return ret;
}

static int
rtl8188e_Efuse_PgPacketWrite(PADAPTER	pAdapter,
			     u8			offset,
			     u8			word_en,
			     u8			*data,
			     bool		bPseudoTest)
{
	int	ret;

	if (bPseudoTest)
		ret = Hal_EfusePgPacketWrite_Pseudo(pAdapter, offset, word_en, data, bPseudoTest);
	else
		ret = Hal_EfusePgPacketWrite(pAdapter, offset, word_en, data, bPseudoTest);
	return ret;
}

static void read_chip_version_8188e(PADAPTER padapter)
{
	u32				value32;
	HAL_DATA_TYPE	*pHalData;

	pHalData = GET_HAL_DATA(padapter);

	value32 = rtw_read32(padapter, REG_SYS_CFG);
	pHalData->version_id.ICType = CHIP_8188E;
	pHalData->version_id.ChipType = ((value32 & RTL_ID) ? TEST_CHIP : NORMAL_CHIP);

	pHalData->version_id.RFType = RF_TYPE_1T1R;
	pHalData->version_id.VendorType = ((value32 & VENDOR_ID) ? CHIP_VENDOR_UMC : CHIP_VENDOR_TSMC);
	pHalData->version_id.CUTVersion = (value32 & CHIP_VER_RTL_MASK) >> CHIP_VER_RTL_SHIFT; /* IC version (CUT) */

	/* For regulator mode. by tynli. 2011.01.14 */
	pHalData->RegulatorMode = ((value32 & TRP_BT_EN) ? RT_LDO_REGULATOR : RT_SWITCHING_REGULATOR);

	pHalData->version_id.ROMVer = 0;	/* ROM code version.	 */
	pHalData->MultiFunc = RT_MULTI_FUNC_NONE;

	rtw_hal_config_rftype(padapter);
	dump_chip_info(pHalData->version_id);
}

void rtl8188e_start_thread(_adapter *padapter)
{
}

void rtl8188e_stop_thread(_adapter *padapter)
{
}

static void hal_notch_filter_8188e(_adapter *adapter, bool enable)
{
	if (enable) {
		RTW_INFO("Enable notch filter\n");
		rtw_write8(adapter, rOFDM0_RxDSP + 1, rtw_read8(adapter, rOFDM0_RxDSP + 1) | BIT1);
	} else {
		RTW_INFO("Disable notch filter\n");
		rtw_write8(adapter, rOFDM0_RxDSP + 1, rtw_read8(adapter, rOFDM0_RxDSP + 1) & ~BIT1);
	}
}

static void update_ra_mask_8188e(_adapter *padapter, struct sta_info *psta, struct macid_cfg *h2c_macid_cfg)
{
	HAL_DATA_TYPE	*hal_data = GET_HAL_DATA(padapter);

	if (hal_data->fw_ractrl == true) {
		u8 arg[4] = {0};

		arg[0] = h2c_macid_cfg->mac_id;/* MACID */
		arg[1] = h2c_macid_cfg->rate_id;
		arg[2] = (h2c_macid_cfg->ignore_bw << 4) | h2c_macid_cfg->short_gi;
		arg[3] =  psta->init_rate;
		rtl8188e_set_raid_cmd(padapter, h2c_macid_cfg->ra_mask, arg, h2c_macid_cfg->bandwidth);
	} else {

#if (RATE_ADAPTIVE_SUPPORT == 1)

		odm_ra_update_rate_info_8188e(
			&(hal_data->odmpriv),
			h2c_macid_cfg->mac_id,
			h2c_macid_cfg->rate_id,
			h2c_macid_cfg->ra_mask,
			h2c_macid_cfg->short_gi
		);

#endif
	}

}

void init_hal_spec_8188e(_adapter *adapter)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);

	hal_spec->ic_name = "rtl8188e";
	hal_spec->macid_num = 64;
	hal_spec->sec_cam_ent_num = 32;
	hal_spec->sec_cap = 0;
	hal_spec->rfpath_num_2g = 1;
	hal_spec->rfpath_num_5g = 0;
	hal_spec->max_tx_cnt = 1;
	hal_spec->tx_nss_num = 1;
	hal_spec->rx_nss_num = 1;
	hal_spec->band_cap = BAND_CAP_2G;
	hal_spec->bw_cap = BW_CAP_20M | BW_CAP_40M;
	hal_spec->port_num = 2;
	hal_spec->proto_cap = PROTO_CAP_11B | PROTO_CAP_11G | PROTO_CAP_11N;
	hal_spec->wl_func = 0
			    | WL_FUNC_P2P
			    | WL_FUNC_MIRACAST
			    | WL_FUNC_TDLS
			    ;
}

#ifdef CONFIG_RFKILL_POLL
bool rtl8188e_gpio_radio_on_off_check(_adapter *adapter, u8 *valid)
{
	u32 tmp32;
	bool ret;

	*valid = 0;
	return false; /* unblock */
}
#endif

void rtl8188e_init_default_value(_adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);

	adapter->registrypriv.wireless_mode = WIRELESS_11BG_24N;
}

void rtl8188e_set_hal_ops(struct hal_ops *pHalFunc)
{
	pHalFunc->dm_init = &rtl8188e_init_dm_priv;
	pHalFunc->dm_deinit = &rtl8188e_deinit_dm_priv;

	pHalFunc->read_chip_version = read_chip_version_8188e;

	pHalFunc->update_ra_mask_handler = update_ra_mask_8188e;

	pHalFunc->set_chnl_bw_handler = &PHY_SetSwChnlBWMode8188E;

	pHalFunc->set_tx_power_level_handler = &PHY_SetTxPowerLevel8188E;
	pHalFunc->get_tx_power_level_handler = &PHY_GetTxPowerLevel8188E;

	pHalFunc->get_tx_power_index_handler = &PHY_GetTxPowerIndex_8188E;

	pHalFunc->hal_dm_watchdog = &rtl8188e_HalDmWatchDog;

	pHalFunc->run_thread = &rtl8188e_start_thread;
	pHalFunc->cancel_thread = &rtl8188e_stop_thread;

	pHalFunc->read_bbreg = &PHY_QueryBBReg8188E;
	pHalFunc->write_bbreg = &PHY_SetBBReg8188E;
	pHalFunc->read_rfreg = &PHY_QueryRFReg8188E;
	pHalFunc->write_rfreg = &PHY_SetRFReg8188E;


	/* Efuse related function */
	pHalFunc->EfusePowerSwitch = &rtl8188e_EfusePowerSwitch;
	pHalFunc->ReadEFuse = &rtl8188e_ReadEFuse;
	pHalFunc->EFUSEGetEfuseDefinition = &rtl8188e_EFUSE_GetEfuseDefinition;
	pHalFunc->EfuseGetCurrentSize = &rtl8188e_EfuseGetCurrentSize;
	pHalFunc->Efuse_PgPacketRead = &rtl8188e_Efuse_PgPacketRead;
	pHalFunc->Efuse_PgPacketWrite = &rtl8188e_Efuse_PgPacketWrite;
	pHalFunc->Efuse_WordEnableDataWrite = &rtl8188e_Efuse_WordEnableDataWrite;

#ifdef DBG_CONFIG_ERROR_DETECT
	pHalFunc->sreset_init_value = &sreset_init_value;
	pHalFunc->sreset_reset_value = &sreset_reset_value;
	pHalFunc->silentreset = &sreset_reset;
	pHalFunc->sreset_xmit_status_check = &rtl8188e_sreset_xmit_status_check;
	pHalFunc->sreset_linked_status_check  = &rtl8188e_sreset_linked_status_check;
	pHalFunc->sreset_get_wifi_status  = &sreset_get_wifi_status;
	pHalFunc->sreset_inprogress = &sreset_inprogress;
#endif /* DBG_CONFIG_ERROR_DETECT */

	pHalFunc->GetHalODMVarHandler = GetHalODMVar;
	pHalFunc->SetHalODMVarHandler = SetHalODMVar;

#ifdef CONFIG_IOL
	pHalFunc->IOL_exec_cmds_sync = &rtl8188e_IOL_exec_cmds_sync;
#endif

	pHalFunc->hal_notch_filter = &hal_notch_filter_8188e;
	pHalFunc->fill_h2c_cmd = &FillH2CCmd_88E;
	pHalFunc->fill_fake_txdesc = &rtl8188e_fill_fake_txdesc;
	pHalFunc->fw_dl = &rtl8188e_FirmwareDownload;
	pHalFunc->hal_get_tx_buff_rsvd_page_num = &GetTxBufferRsvdPageNum8188E;

#ifdef CONFIG_GPIO_API
        pHalFunc->hal_gpio_func_check = &rtl8188e_GpioFuncCheck;
#endif
#ifdef CONFIG_RFKILL_POLL
	pHalFunc->hal_radio_onoff_check = rtl8188e_gpio_radio_on_off_check;
#endif
}

u8 GetEEPROMSize8188E(PADAPTER padapter)
{
	u8 size = 0;
	u32	cr;

	cr = rtw_read16(padapter, REG_9346CR);
	/* 6: EEPROM used is 93C46, 4: boot from E-Fuse. */
	size = (cr & BOOT_FROM_EEPROM) ? 6 : 4;

	RTW_INFO("EEPROM type is %s\n", size == 4 ? "E-FUSE" : "93C46");

	return size;
}

/* -------------------------------------------------------------------------
 *
 * LLT R/W/Init function
 *
 * ------------------------------------------------------------------------- */
static s32 _LLTWrite(PADAPTER padapter, u32 address, u32 data)
{
	s32	status = _SUCCESS;
	s8	count = POLLING_LLT_THRESHOLD;
	u32	value = _LLT_INIT_ADDR(address) | _LLT_INIT_DATA(data) | _LLT_OP(_LLT_WRITE_ACCESS);

	rtw_write32(padapter, REG_LLT_INIT, value);

	/* polling */
	do {
		value = rtw_read32(padapter, REG_LLT_INIT);
		if (_LLT_NO_ACTIVE == _LLT_OP_VALUE(value))
			break;
	} while (--count);

	if (count <= 0) {
		RTW_INFO("Failed to polling write LLT done at address %d!\n", address);
		status = _FAIL;
	}

	return status;
}

static u8 _LLTRead(PADAPTER padapter, u32 address)
{
	s32	count = POLLING_LLT_THRESHOLD;
	u32	value = _LLT_INIT_ADDR(address) | _LLT_OP(_LLT_READ_ACCESS);
	u16	LLTReg = REG_LLT_INIT;


	rtw_write32(padapter, LLTReg, value);

	/* polling and get value */
	do {
		value = rtw_read32(padapter, LLTReg);
		if (_LLT_NO_ACTIVE == _LLT_OP_VALUE(value))
			return (u8)value;
	} while (--count);




	return 0xFF;
}

s32 InitLLTTable(PADAPTER padapter, u8 txpktbuf_bndy)
{
	s32	status = _FAIL;
	u32	i;
	u32	Last_Entry_Of_TxPktBuf = LAST_ENTRY_OF_TX_PKT_BUFFER_8188E(padapter);/* 176, 22k */
	HAL_DATA_TYPE *pHalData	= GET_HAL_DATA(padapter);

#if defined(CONFIG_IOL_LLT)
	if (rtw_IOL_applied(padapter))
		status = iol_InitLLTTable(padapter, txpktbuf_bndy);
	else
#endif
	{
		for (i = 0; i < (txpktbuf_bndy - 1); i++) {
			status = _LLTWrite(padapter, i, i + 1);
			if (_SUCCESS != status)
				return status;
		}

		/* end of list */
		status = _LLTWrite(padapter, (txpktbuf_bndy - 1), 0xFF);
		if (_SUCCESS != status)
			return status;

		/* Make the other pages as ring buffer */
		/* This ring buffer is used as beacon buffer if we config this MAC as two MAC transfer. */
		/* Otherwise used as local loopback buffer. */
		for (i = txpktbuf_bndy; i < Last_Entry_Of_TxPktBuf; i++) {
			status = _LLTWrite(padapter, i, (i + 1));
			if (_SUCCESS != status)
				return status;
		}

		/* Let last entry point to the start entry of ring buffer */
		status = _LLTWrite(padapter, Last_Entry_Of_TxPktBuf, txpktbuf_bndy);
		if (_SUCCESS != status)
			return status;
	}

	return status;
}

void
Hal_InitPGData88E(PADAPTER	padapter)
{

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u32			i;
	u16			value16;

	if (false == pHalData->bautoload_fail_flag) {
		/* autoload OK. */
		if (is_boot_from_eeprom(padapter)) {
			/* Read all Content from EEPROM or EFUSE. */
			for (i = 0; i < HWSET_MAX_SIZE; i += 2) {
				/*				value16 = EF2Byte(ReadEEprom(pAdapter, (u16) (i>>1)));
				 *				*((u16*)(&PROMContent[i])) = value16; */
			}
		} else {
			/* Read EFUSE real map to shadow. */
			EFUSE_ShadowMapUpdate(padapter, EFUSE_WIFI, false);
		}
	} else {
		/* autoload fail */
		/*		pHalData->AutoloadFailFlag = true; */
		/* update to default value 0xFF */
		if (!is_boot_from_eeprom(padapter))
			EFUSE_ShadowMapUpdate(padapter, EFUSE_WIFI, false);
	}

#ifdef CONFIG_EFUSE_CONFIG_FILE
	if (check_phy_efuse_tx_power_info_valid(padapter) == false) {
		if (Hal_readPGDataFromConfigFile(padapter) != _SUCCESS)
			RTW_ERR("invalid phy efuse and read from file fail, will use driver default!!\n");
	}
#endif
}

void
Hal_EfuseParseIDCode88E(
	PADAPTER	padapter,
	u8			*hwinfo
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u16			EEPROMId;


	/* Checl 0x8129 again for making sure autoload status!! */
	EEPROMId = le16_to_cpu(*((__le16 *)hwinfo));
	if (EEPROMId != RTL_EEPROM_ID) {
		RTW_INFO("EEPROM ID(%#x) is invalid!!\n", EEPROMId);
		pHalData->bautoload_fail_flag = true;
	} else
		pHalData->bautoload_fail_flag = false;

	RTW_INFO("EEPROM ID=0x%04x\n", EEPROMId);
}

void Hal_ReadPowerSavingMode88E(
	PADAPTER		padapter,
	u8			*hwinfo,
	bool			AutoLoadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	u8 tmpvalue;

	if (AutoLoadFail) {
		pwrctl->bHWPowerdown = false;
		pwrctl->bSupportRemoteWakeup = false;
	} else	{

		/* hw power down mode selection , 0:rf-off / 1:power down */

		if (padapter->registrypriv.hwpdn_mode == 2)
			pwrctl->bHWPowerdown = (hwinfo[EEPROM_RF_FEATURE_OPTION_88E] & BIT4);
		else
			pwrctl->bHWPowerdown = padapter->registrypriv.hwpdn_mode;

		/* decide hw if support remote wakeup function */
		/* if hw supported, 8051 (SIE) will generate WeakUP signal( D+/D- toggle) when autoresume */
		pwrctl->bSupportRemoteWakeup = (hwinfo[EEPROM_USB_OPTIONAL_FUNCTION0] & BIT1) ? true : false;

		RTW_INFO("%s...bHWPwrPindetect(%x)-bHWPowerdown(%x) ,bSupportRemoteWakeup(%x)\n", __func__,
			pwrctl->bHWPwrPindetect, pwrctl->bHWPowerdown, pwrctl->bSupportRemoteWakeup);

		RTW_INFO("### PS params=>  power_mgnt(%x),usbss_enable(%x) ###\n", padapter->registrypriv.power_mgnt, padapter->registrypriv.usbss_enable);

	}

}

void
Hal_ReadTxPowerInfo88E(
	PADAPTER		padapter,
	u8				*PROMContent,
	bool			AutoLoadFail
)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	TxPowerInfo24G pwrInfo24G;
	
	hal_load_txpwr_info(padapter, &pwrInfo24G, NULL, PROMContent);

	/* 2010/10/19 MH Add Regulator recognize for EU. */
	if (!AutoLoadFail) {
		struct registry_priv  *registry_par = &padapter->registrypriv;

		if (PROMContent[EEPROM_RF_BOARD_OPTION_88E] == 0xFF)
			pHalData->EEPROMRegulatory = (EEPROM_DEFAULT_BOARD_OPTION & 0x7);	/* bit0~2 */
		else
			pHalData->EEPROMRegulatory = (PROMContent[EEPROM_RF_BOARD_OPTION_88E] & 0x7);	/* bit0~2 */

	} else
		pHalData->EEPROMRegulatory = 0;
	RTW_INFO("EEPROMRegulatory = 0x%x\n", pHalData->EEPROMRegulatory);

}


void
Hal_EfuseParseXtal_8188E(
	PADAPTER		pAdapter,
	u8			*hwinfo,
	bool		AutoLoadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if (!AutoLoadFail) {
		pHalData->crystal_cap = hwinfo[EEPROM_XTAL_88E];
		if (pHalData->crystal_cap == 0xFF)
			pHalData->crystal_cap = EEPROM_Default_CrystalCap_88E;
	} else
		pHalData->crystal_cap = EEPROM_Default_CrystalCap_88E;
	RTW_INFO("crystal_cap: 0x%2x\n", pHalData->crystal_cap);
}

void
Hal_ReadPAType_8188E(
	PADAPTER	Adapter,
	u8			*PROMContent,
	bool		AutoloadFail
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	u8			PA_LNAType_2G = 0;

	if (!AutoloadFail) {
		if (GetRegAmplifierType2G(Adapter) == 0) { /* AUTO*/

			/* PA & LNA Type */
			PA_LNAType_2G = LE_BITS_TO_1BYTE(&PROMContent[EEPROM_RFE_OPTION_8188E], 2, 2); /* 0xCA[3:2] */
			/*
				ePA/eLNA sel.(ePA+eLNA=0x0, ePA+iLNA enable = 0x1, iPA+eLNA enable =0x2, iPA+iLNA=0x3)
			*/
			switch (PA_LNAType_2G) {
			case 0:
				pHalData->ExternalPA_2G  = 1;
				pHalData->ExternalLNA_2G = 1;
				break;
			case 1:
				pHalData->ExternalPA_2G  = 1;
				pHalData->ExternalLNA_2G = 0;
				break;
			case 2:
				pHalData->ExternalPA_2G  = 0;
				pHalData->ExternalLNA_2G = 1;
				break;
			case 3:
			default:
				pHalData->ExternalPA_2G  = 0;
				pHalData->ExternalLNA_2G = 0;
				break;
			}
		} else {
			pHalData->ExternalPA_2G  = (GetRegAmplifierType2G(Adapter) & ODM_BOARD_EXT_PA)  ? 1 : 0;
			pHalData->ExternalLNA_2G = (GetRegAmplifierType2G(Adapter) & ODM_BOARD_EXT_LNA) ? 1 : 0;
		}
	} else {
		pHalData->ExternalPA_2G  = EEPROM_Default_PAType;
		pHalData->external_pa_5g  = EEPROM_Default_PAType;
		pHalData->ExternalLNA_2G = EEPROM_Default_LNAType;
		pHalData->external_lna_5g = EEPROM_Default_LNAType;

		if (GetRegAmplifierType2G(Adapter) == 0) {
			/* AUTO*/
			pHalData->ExternalPA_2G  = EEPROM_Default_PAType;
			pHalData->ExternalLNA_2G = EEPROM_Default_LNAType;
		} else {
			pHalData->ExternalPA_2G  = (GetRegAmplifierType2G(Adapter) & ODM_BOARD_EXT_PA)  ? 1 : 0;
			pHalData->ExternalLNA_2G = (GetRegAmplifierType2G(Adapter) & ODM_BOARD_EXT_LNA) ? 1 : 0;
		}
	}
	RTW_INFO("pHalData->ExternalPA_2G = %d , pHalData->ExternalLNA_2G = %d\n",  pHalData->ExternalPA_2G, pHalData->ExternalLNA_2G);
}

void
Hal_ReadAmplifierType_8188E(
	PADAPTER	Adapter,
	u8 *		PROMContent,
	bool		AutoloadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	GLNA_type = 0;

	if (!AutoloadFail) {
		if (GetRegGLNAType(Adapter) == 0) /* AUTO */
			GLNA_type = LE_BITS_TO_1BYTE(&PROMContent[EEPROM_RFE_OPTION_8188E], 4, 3); /* 0xCA[6:4] */
		else
			GLNA_type  = GetRegGLNAType(Adapter) & 0x7;
	} else {
		if (GetRegGLNAType(Adapter) == 0) /* AUTO */
			GLNA_type = 0;
		else
			GLNA_type  = GetRegGLNAType(Adapter) & 0x7;
	}
	/*
		Ext-LNA Gain sel.(form 10dB to 24dB, 1table/2dB,ext: 000=10dB, 001=12dB...)
	*/
	switch (GLNA_type) {
	case 0:
		pHalData->TypeGLNA = 0x1; /* (10dB) */
		break;
	case 2:
		pHalData->TypeGLNA = 0x2; /* (14dB) */
		break;
	default:
		pHalData->TypeGLNA = 0x0; /* (others not support) */
		break;
	}
	RTW_INFO("pHalData->TypeGLNA is 0x%x\n", pHalData->TypeGLNA);
}

void
Hal_ReadRFEType_8188E(
	PADAPTER	Adapter,
	u8 *		PROMContent,
	bool		AutoloadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	/* Keep the same flow as 8192EU to be extensible */
	const u8 RFETypeMaxVal = 1, RFETypeMask = 0x1;

	if (!AutoloadFail) {
		if (GetRegRFEType(Adapter) != 64) {
			pHalData->rfe_type = GetRegRFEType(Adapter);
			/*
				Above 1, rfe_type is filled the default value.
			*/
			if (pHalData->rfe_type > RFETypeMaxVal)
				pHalData->rfe_type = EEPROM_DEFAULT_RFE_OPTION_8188E;

		} else if ((0xFF == PROMContent[EEPROM_RFE_OPTION_8188E]) ||
			((pHalData->ExternalPA_2G == 0) && (pHalData->ExternalLNA_2G == 0)))
			pHalData->rfe_type = EEPROM_DEFAULT_RFE_OPTION_8188E;
		else {
			/*
				type 0:0x00 for 88EE/ER_HP RFE control
			*/
			pHalData->rfe_type = PROMContent[EEPROM_RFE_OPTION_8188E] & RFETypeMask; /* 0xCA[1:0] */
		}
	} else {
		if (GetRegRFEType(Adapter) != 64) {
			pHalData->rfe_type = GetRegRFEType(Adapter);
			/*
				 Above 3, rfe_type is filled the default value.
			*/
			if (pHalData->rfe_type > RFETypeMaxVal)
				pHalData->rfe_type = EEPROM_DEFAULT_RFE_OPTION_8188E;

		} else
			pHalData->rfe_type = EEPROM_DEFAULT_RFE_OPTION_8188E;

	}

	RTW_INFO("pHalData->rfe_type is 0x%x\n", pHalData->rfe_type);
}

void
Hal_EfuseParseBoardType88E(
	PADAPTER		pAdapter,
	u8				*hwinfo,
	bool			AutoLoadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if (!AutoLoadFail) {
		pHalData->InterfaceSel = ((hwinfo[EEPROM_RF_BOARD_OPTION_88E] & 0xE0) >> 5);
		if (hwinfo[EEPROM_RF_BOARD_OPTION_88E] == 0xFF)
			pHalData->InterfaceSel = (EEPROM_DEFAULT_BOARD_OPTION & 0xE0) >> 5;
	} else
		pHalData->InterfaceSel = 0;
	RTW_INFO("Board Type: 0x%2x\n", pHalData->InterfaceSel);
}

void
Hal_EfuseParseEEPROMVer88E(
	PADAPTER		padapter,
	u8			*hwinfo,
	bool			AutoLoadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if (!AutoLoadFail) {
		pHalData->EEPROMVersion = hwinfo[EEPROM_VERSION_88E];
		if (pHalData->EEPROMVersion == 0xFF)
			pHalData->EEPROMVersion = EEPROM_Default_Version;
	} else
		pHalData->EEPROMVersion = 1;
}

void
rtl8188e_EfuseParseChnlPlan(
	PADAPTER		padapter,
	u8			*hwinfo,
	bool			AutoLoadFail
)
{
	padapter->mlmepriv.ChannelPlan = hal_com_config_channel_plan(
			padapter
			, hwinfo ? &hwinfo[EEPROM_COUNTRY_CODE_88E] : NULL
			, hwinfo ? hwinfo[EEPROM_ChannelPlan_88E] : 0xFF
			, padapter->registrypriv.alpha2
			, padapter->registrypriv.channel_plan
			, RTW_CHPLAN_WORLD_NULL
			, AutoLoadFail
					 );
}

void
Hal_EfuseParseCustomerID88E(
	PADAPTER		padapter,
	u8			*hwinfo,
	bool			AutoLoadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if (!AutoLoadFail) {
		pHalData->EEPROMCustomerID = hwinfo[EEPROM_CustomID_88E];
		/* pHalData->EEPROMSubCustomerID = hwinfo[EEPROM_CustomID_88E]; */
	} else {
		pHalData->EEPROMCustomerID = 0;
		pHalData->EEPROMSubCustomerID = 0;
	}
	RTW_INFO("EEPROM Customer ID: 0x%2x\n", pHalData->EEPROMCustomerID);
	/* RTW_INFO("EEPROM SubCustomer ID: 0x%02x\n", pHalData->EEPROMSubCustomerID); */
}


void
Hal_ReadAntennaDiversity88E(
	PADAPTER		pAdapter,
	u8				*PROMContent,
	bool			AutoLoadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct registry_priv	*registry_par = &pAdapter->registrypriv;

	if (!AutoLoadFail) {
		/* Antenna Diversity setting. */
		if (registry_par->antdiv_cfg == 2) { /* 2:By EFUSE */
			pHalData->AntDivCfg = (PROMContent[EEPROM_RF_BOARD_OPTION_88E] & 0x18) >> 3;
			if (PROMContent[EEPROM_RF_BOARD_OPTION_88E] == 0xFF)
				pHalData->AntDivCfg = (EEPROM_DEFAULT_BOARD_OPTION & 0x18) >> 3;
		} else {
			pHalData->AntDivCfg = registry_par->antdiv_cfg ;  /* 0:OFF , 1:ON, 2:By EFUSE */
		}

		if (registry_par->antdiv_type == 0) { /* If TRxAntDivType is AUTO in advanced setting, use EFUSE value instead. */
			pHalData->TRxAntDivType = PROMContent[EEPROM_RF_ANTENNA_OPT_88E];
			if (pHalData->TRxAntDivType == 0xFF)
				pHalData->TRxAntDivType = CG_TRX_HW_ANTDIV; /* For 88EE, 1Tx and 1RxCG are fixed.(1Ant, Tx and RxCG are both on aux port) */
		} else
			pHalData->TRxAntDivType = registry_par->antdiv_type ;

		if (pHalData->TRxAntDivType == CG_TRX_HW_ANTDIV || pHalData->TRxAntDivType == CGCS_RX_HW_ANTDIV)
			pHalData->AntDivCfg = 1; /* 0xC1[3] is ignored. */
	} else
		pHalData->AntDivCfg = 0;

	RTW_INFO("EEPROM : AntDivCfg = %x, TRxAntDivType = %x\n", pHalData->AntDivCfg, pHalData->TRxAntDivType);


}

void
Hal_ReadThermalMeter_88E(
	PADAPTER	Adapter,
	u8			*PROMContent,
	bool	AutoloadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			tempval;

	/*  */
	/* ThermalMeter from EEPROM */
	/*  */
	if (!AutoloadFail)
		pHalData->eeprom_thermal_meter = PROMContent[EEPROM_THERMAL_METER_88E];
	else
		pHalData->eeprom_thermal_meter = EEPROM_Default_ThermalMeter_88E;
	/* pHalData->eeprom_thermal_meter = (tempval&0x1f);	 */ /* [4:0] */

	if (pHalData->eeprom_thermal_meter == 0xff || AutoloadFail) {
		pHalData->odmpriv.rf_calibrate_info.is_apk_thermal_meter_ignore = true;
		pHalData->eeprom_thermal_meter = EEPROM_Default_ThermalMeter_88E;
	}

	/* pHalData->ThermalMeter[0] = pHalData->eeprom_thermal_meter;	 */
	RTW_INFO("ThermalMeter = 0x%x\n", pHalData->eeprom_thermal_meter);

}

#ifdef CONFIG_RF_POWER_TRIM
void Hal_ReadRFGainOffset(
		PADAPTER	Adapter,
		u8		*PROMContent,
		bool		AutoloadFail)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8 thermal_offset = 0;
	/*  */
	/* BB_RF Gain Offset from EEPROM */
	/*  */

	if (!AutoloadFail) {
		pHalData->EEPROMRFGainOffset = PROMContent[EEPROM_RF_GAIN_OFFSET];

		if ((pHalData->EEPROMRFGainOffset  != 0xFF) &&
		    (pHalData->EEPROMRFGainOffset & BIT4))
			efuse_OneByteRead(Adapter, EEPROM_RF_GAIN_VAL, &pHalData->EEPROMRFGainVal, false);
		else {
			pHalData->EEPROMRFGainOffset = 0;
			pHalData->EEPROMRFGainVal = 0;
		}

		RTW_INFO("pHalData->EEPROMRFGainVal=%x\n", pHalData->EEPROMRFGainVal);
	} else {
		efuse_OneByteRead(Adapter, EEPROM_RF_GAIN_VAL, &pHalData->EEPROMRFGainVal, false);

		if (pHalData->EEPROMRFGainVal != 0xFF)
			pHalData->EEPROMRFGainOffset = BIT4;
		else
			pHalData->EEPROMRFGainOffset = 0;
		RTW_INFO("else AutoloadFail =%x,\n", AutoloadFail);
	}

	if (Adapter->registrypriv.RegPwrTrimEnable == 1) {
		efuse_OneByteRead(Adapter, EEPROM_RF_GAIN_VAL, &pHalData->EEPROMRFGainVal, false);
		RTW_INFO("pHalData->EEPROMRFGainVal=%x\n", pHalData->EEPROMRFGainVal);

	}
	/*  */
	/* BB_RF Thermal Offset from EEPROM */
	/*  */
	if (((pHalData->EEPROMRFGainOffset != 0xFF) && (pHalData->EEPROMRFGainOffset & BIT4)) || (Adapter->registrypriv.RegPwrTrimEnable == 1)) {

		efuse_OneByteRead(Adapter, EEPROM_THERMAL_OFFSET, &thermal_offset, false);
		if (thermal_offset != 0xFF) {
			if (thermal_offset & BIT0)
				pHalData->eeprom_thermal_meter += ((thermal_offset >> 1) & 0x0F);
			else
				pHalData->eeprom_thermal_meter -= ((thermal_offset >> 1) & 0x0F);

			RTW_INFO("%s =>thermal_offset:0x%02x pHalData->eeprom_thermal_meter=0x%02x\n", __func__ , thermal_offset, pHalData->eeprom_thermal_meter);
		}
	}

	RTW_INFO("%s => EEPRORFGainOffset = 0x%02x,EEPROMRFGainVal=0x%02x,thermal_offset:0x%02x\n",
		__func__, pHalData->EEPROMRFGainOffset, pHalData->EEPROMRFGainVal, thermal_offset);

}

#endif /*CONFIG_RF_POWER_TRIM*/

bool HalDetectPwrDownMode88E(PADAPTER Adapter)
{
	u8 tmpvalue = 0;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(Adapter);

	EFUSE_ShadowRead(Adapter, 1, EEPROM_RF_FEATURE_OPTION_88E, (u32 *)&tmpvalue);

	/* 2010/08/25 MH INF priority > PDN Efuse value. */
	if (tmpvalue & BIT(4) && pwrctrlpriv->reg_pdnmode)
		pHalData->pwrdown = true;
	else
		pHalData->pwrdown = false;

	RTW_INFO("HalDetectPwrDownMode(): PDN=%d\n", pHalData->pwrdown);

	return pHalData->pwrdown;
}	/* HalDetectPwrDownMode */

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
void Hal_DetectWoWMode(PADAPTER pAdapter)
{
	adapter_to_pwrctl(pAdapter)->bSupportRemoteWakeup = true;
}
#endif

/* ************************************************************************************
 *
 * 20100209 Joseph:
 * This function is used only for 92C to set REG_BCN_CTRL(0x550) register.
 * We just reserve the value of the register in variable pHalData->RegBcnCtrlVal and then operate
 * the value of the register via atomic operation.
 * This prevents from race condition when setting this register.
 * The value of pHalData->RegBcnCtrlVal is initialized in HwConfigureRTL8192CE() function.
 *   */
void SetBcnCtrlReg(
	PADAPTER	padapter,
	u8		SetBits,
	u8		ClearBits)
{
	PHAL_DATA_TYPE pHalData;


	pHalData = GET_HAL_DATA(padapter);

	pHalData->RegBcnCtrlVal |= SetBits;
	pHalData->RegBcnCtrlVal &= ~ClearBits;

	rtw_write8(padapter, REG_BCN_CTRL, (u8)pHalData->RegBcnCtrlVal);
}

void _InitTransferPageSize(PADAPTER padapter)
{
	/* Tx page size is always 128. */

	u8 value8;
	value8 = _PSRX(PBP_128) | _PSTX(PBP_128);
	rtw_write8(padapter, REG_PBP, value8);
}


static void hw_var_set_monitor(PADAPTER Adapter, u8 variable, u8 *val)
{
	u32	value_rcr, rcr_bits;
	u16	value_rxfltmap2;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv *pmlmepriv = &(Adapter->mlmepriv);

	if (*((u8 *)val) == _HW_STATE_MONITOR_) {

		/* Receive all type */
		rcr_bits = RCR_AAP | RCR_APM | RCR_AM | RCR_AB | RCR_APWRMGT | RCR_ADF | RCR_ACF | RCR_AMF | RCR_APP_PHYST_RXFF;

		/* Append FCS */
		rcr_bits |= RCR_APPFCS;

		/* Receive all data frames */
		value_rxfltmap2 = 0xFFFF;

		value_rcr = rcr_bits;
		rtw_write32(Adapter, REG_RCR, value_rcr);

		rtw_write16(Adapter, REG_RXFLTMAP2, value_rxfltmap2);
	} else {
		/* do nothing */
	}
}

static void hw_var_set_opmode(PADAPTER Adapter, u8 variable, u8 *val)
{
	u8	val8;
	u8	mode = *((u8 *)val);
	static u8 isMonitor = false;

	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);

	if (isMonitor == true) {
		/* reset RCR */
		rtw_write32(Adapter, REG_RCR, pHalData->ReceiveConfig);
		isMonitor = false;
	}

	RTW_INFO(ADPT_FMT "- Port-%d  set opmode = %d\n", ADPT_ARG(Adapter),
		 get_hw_port(Adapter), mode);

	if (mode == _HW_STATE_MONITOR_) {
		isMonitor = true;
		/* set net_type */
		Set_MSR(Adapter, _HW_STATE_NOLINK_);

		hw_var_set_monitor(Adapter, variable, val);
		return;
	}

	rtw_hal_set_hwreg(Adapter, HW_VAR_MAC_ADDR, adapter_mac_addr(Adapter)); /* set mac addr to mac register */

#ifdef CONFIG_CONCURRENT_MODE
	if (Adapter->hw_port == HW_PORT1) {
		/* disable Port1 TSF update */
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1) | BIT(4));

		/* set net_type		 */
		Set_MSR(Adapter, mode);

		if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_)) {
			if (!rtw_mi_check_status(Adapter, MI_AP_MODE))	 {
#ifdef CONFIG_INTERRUPT_BASED_TXBCN
#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
				rtw_write8(Adapter, REG_DRVERLYINT, 0x05);/* restore early int time to 5ms */

				UpdateInterruptMask8188EU(Adapter, true, 0, IMR_BCNDMAINT0_88E);

#endif /* CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
				UpdateInterruptMask8188EU(Adapter, true , 0, (IMR_TBDER_88E | IMR_TBDOK_88E));

#endif/* CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR */
#endif /* CONFIG_INTERRUPT_BASED_TXBCN		 */

				StopTxBeacon(Adapter);
			}

			rtw_write8(Adapter, REG_BCN_CTRL_1, 0x11); /* disable atim wnd and disable beacon function */
			/* rtw_write8(Adapter,REG_BCN_CTRL_1, 0x18); */
		} else if ((mode == _HW_STATE_ADHOC_) /*|| (mode == _HW_STATE_AP_)*/) {
			/* Beacon is polled to TXBUF */
			rtw_write32(Adapter, REG_CR, rtw_read32(Adapter, REG_CR) | BIT(8));

			ResumeTxBeacon(Adapter);
			rtw_write8(Adapter, REG_BCN_CTRL_1, 0x1a);
			/* BIT4 - If set 0, hw will clr bcnq when tx becon ok/fail or port 1 */
			rtw_write8(Adapter, REG_MBID_NUM, rtw_read8(Adapter, REG_MBID_NUM) | BIT(3) | BIT(4));
		} else if (mode == _HW_STATE_AP_) {
#ifdef CONFIG_INTERRUPT_BASED_TXBCN
#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			UpdateInterruptMask8188EU(Adapter, true , IMR_BCNDMAINT0_88E, 0);
#endif/* CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
			UpdateInterruptMask8188EU(Adapter, true , (IMR_TBDER_88E | IMR_TBDOK_88E), 0);
#endif/* CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR */

#endif /* CONFIG_INTERRUPT_BASED_TXBCN */

			ResumeTxBeacon(Adapter);

			rtw_write8(Adapter, REG_BCN_CTRL_1, 0x12);

			/* Beacon is polled to TXBUF */
			rtw_write32(Adapter, REG_CR, rtw_read32(Adapter, REG_CR) | BIT(8));

			/* Set RCR */
			/* rtw_write32(padapter, REG_RCR, 0x70002a8e); */ /* CBSSID_DATA must set to 0 */
			if (Adapter->registrypriv.wifi_spec)
				/* for 11n Logo 4.2.31/4.2.32, disable BSSID BCN check for AP mode */
				rtw_write32(Adapter, REG_RCR, (0x7000208e & ~(RCR_CBSSID_BCN)));
			else	
				rtw_write32(Adapter, REG_RCR, 0x7000208e);/* CBSSID_DATA must set to 0,Reject ICV_ERROR packets */

			/* enable to rx data frame				 */
			rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);
			/* enable to rx ps-poll */
			rtw_write16(Adapter, REG_RXFLTMAP1, 0x0400);

			/* Beacon Control related register for first time */
			rtw_write8(Adapter, REG_BCNDMATIM, 0x02); /* 2ms		 */
			rtw_write8(Adapter, REG_DRVERLYINT, 0x05);/* 5ms */
			/* rtw_write8(Adapter, REG_BCN_MAX_ERR, 0xFF); */
			rtw_write8(Adapter, REG_ATIMWND_1, 0x0c); /* 13ms for port1 */
			rtw_write16(Adapter, REG_BCNTCFG, 0x00);

			/* TBTT setup time:128 us */
			rtw_write8(Adapter, REG_TBTT_PROHIBIT, 0x04);

			/*TBTT hold time :4ms 0x540[19:8]*/
			rtw_write8(Adapter, REG_TBTT_PROHIBIT + 1,
				TBTT_PROBIHIT_HOLD_TIME & 0xFF);
			rtw_write8(Adapter, REG_TBTT_PROHIBIT + 2,
				(rtw_read8(Adapter, REG_TBTT_PROHIBIT + 2) & 0xF0) | (TBTT_PROBIHIT_HOLD_TIME >> 8));

			rtw_write16(Adapter, REG_TSFTR_SYN_OFFSET, 0x7fff);/* +32767 (~32ms) */

			/* reset TSF2	 */
			rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(1));


			/* BIT4 - If set 0, hw will clr bcnq when tx becon ok/fail or port 1 */
			rtw_write8(Adapter, REG_MBID_NUM, rtw_read8(Adapter, REG_MBID_NUM) | BIT(3) | BIT(4));
			/* enable BCN1 Function for if2 */
			/* don't enable update TSF1 for if2 (due to TSF update when beacon/probe rsp are received) */
			rtw_write8(Adapter, REG_BCN_CTRL_1, (DIS_TSF_UDT0_NORMAL_CHIP | EN_BCN_FUNCTION | EN_TXBCN_RPT | BIT(1)));

			if (!rtw_mi_buddy_check_fwstate(Adapter, WIFI_FW_ASSOC_SUCCESS))
				rtw_write8(Adapter, REG_BCN_CTRL,
					rtw_read8(Adapter, REG_BCN_CTRL) & ~EN_BCN_FUNCTION);

			/* BCN1 TSF will sync to BCN0 TSF with offset(0x518) if if1_sta linked */
			/* rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|BIT(5)); */
			/* rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(3)); */

			/* dis BCN0 ATIM  WND if if1 is station */
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL) | BIT(0));

#ifdef CONFIG_TSF_RESET_OFFLOAD
			/* Reset TSF for STA+AP concurrent mode */
			if (rtw_mi_buddy_check_fwstate(Adapter, (WIFI_STATION_STATE | WIFI_ASOC_STATE))) {
				if (reset_tsf(Adapter, HW_PORT1) == false)
					RTW_INFO("ERROR! %s()-%d: Reset port1 TSF fail\n",
						 __func__, __LINE__);
			}
#endif /* CONFIG_TSF_RESET_OFFLOAD */
		}
	} else	/* (Adapter->hw_port == HW_PORT1)*/
#endif /* CONFIG_CONCURRENT_MODE */
	{
#ifdef CONFIG_MI_WITH_MBSSID_CAM /*For Port0 - MBSS CAM*/
		hw_var_set_opmode_mbid(Adapter, mode);
#else
		/* disable Port0 TSF update */
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL) | BIT(4));

		/* set net_type */
		Set_MSR(Adapter, mode);

		if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_)) {
#ifdef CONFIG_CONCURRENT_MODE
			if (!rtw_mi_check_status(Adapter, MI_AP_MODE))
#endif /*CONFIG_CONCURRENT_MODE*/
			{
#ifdef CONFIG_INTERRUPT_BASED_TXBCN
#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
				rtw_write8(Adapter, REG_DRVERLYINT, 0x05);/* restore early int time to 5ms	 */
				UpdateInterruptMask8188EU(Adapter, true, 0, IMR_BCNDMAINT0_88E);
#endif/* CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
				UpdateInterruptMask8188EU(Adapter, true , 0, (IMR_TBDER_88E | IMR_TBDOK_88E));
#endif /* CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR */

#endif /* CONFIG_INTERRUPT_BASED_TXBCN		 */
				StopTxBeacon(Adapter);
			}

			rtw_write8(Adapter, REG_BCN_CTRL, 0x19); /* disable atim wnd */
			/* rtw_write8(Adapter,REG_BCN_CTRL, 0x18); */
		} else if ((mode == _HW_STATE_ADHOC_) /*|| (mode == _HW_STATE_AP_)*/) {
			/* Beacon is polled to TXBUF */
			rtw_write16(Adapter, REG_CR, rtw_read16(Adapter, REG_CR) | BIT(8));

			ResumeTxBeacon(Adapter);
			rtw_write8(Adapter, REG_BCN_CTRL, 0x1a);
			/* BIT3 - If set 0, hw will clr bcnq when tx becon ok/fail or port 0 */
			rtw_write8(Adapter, REG_MBID_NUM, rtw_read8(Adapter, REG_MBID_NUM) | BIT(3) | BIT(4));
		} else if (mode == _HW_STATE_AP_) {
#ifdef CONFIG_INTERRUPT_BASED_TXBCN
#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			UpdateInterruptMask8188EU(Adapter, true , IMR_BCNDMAINT0_88E, 0);
#endif/* CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
			UpdateInterruptMask8188EU(Adapter, true , (IMR_TBDER_88E | IMR_TBDOK_88E), 0);
#endif/* CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR */

#endif /* CONFIG_INTERRUPT_BASED_TXBCN */

			ResumeTxBeacon(Adapter);

			rtw_write8(Adapter, REG_BCN_CTRL, 0x12);

			/* Beacon is polled to TXBUF */
			rtw_write32(Adapter, REG_CR, rtw_read32(Adapter, REG_CR) | BIT(8));

			/* Set RCR */
			/* rtw_write32(padapter, REG_RCR, 0x70002a8e); */ /* CBSSID_DATA must set to 0 */
			if (Adapter->registrypriv.wifi_spec)
				/* for 11n Logo 4.2.31/4.2.32, disable BSSID BCN check for AP mode */
				rtw_write32(Adapter, REG_RCR, (0x7000208e & ~(RCR_CBSSID_BCN)));
			else
				rtw_write32(Adapter, REG_RCR, 0x7000208e);/* CBSSID_DATA must set to 0,reject ICV_ERR packet */
			/* enable to rx data frame */
			rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);
			/* enable to rx ps-poll */
			rtw_write16(Adapter, REG_RXFLTMAP1, 0x0400);

			/* Beacon Control related register for first time */
			rtw_write8(Adapter, REG_BCNDMATIM, 0x02); /* 2ms			 */
			rtw_write8(Adapter, REG_DRVERLYINT, 0x05);/* 5ms */
			/* rtw_write8(Adapter, REG_BCN_MAX_ERR, 0xFF); */
			rtw_write8(Adapter, REG_ATIMWND, 0x0c); /* 13ms */
			rtw_write16(Adapter, REG_BCNTCFG, 0x00);

			/* TBTT setup time:128 us */
			rtw_write8(Adapter, REG_TBTT_PROHIBIT, 0x04);

			/*TBTT hold time :4ms 0x540[19:8]*/
			rtw_write8(Adapter, REG_TBTT_PROHIBIT + 1,
				TBTT_PROBIHIT_HOLD_TIME & 0xFF);
			rtw_write8(Adapter, REG_TBTT_PROHIBIT + 2,
				(rtw_read8(Adapter, REG_TBTT_PROHIBIT + 2) & 0xF0) | (TBTT_PROBIHIT_HOLD_TIME >> 8));

			rtw_write16(Adapter, REG_TSFTR_SYN_OFFSET, 0x7fff);/* +32767 (~32ms) */

			/* reset TSF */
			rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(0));

			/* BIT3 - If set 0, hw will clr bcnq when tx becon ok/fail or port 0 */
			rtw_write8(Adapter, REG_MBID_NUM, rtw_read8(Adapter, REG_MBID_NUM) | BIT(3) | BIT(4));

			/* enable BCN0 Function for if1 */
			/* don't enable update TSF0 for if1 (due to TSF update when beacon/probe rsp are received) */
#if defined(CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR)
			rtw_write8(Adapter, REG_BCN_CTRL, (DIS_TSF_UDT0_NORMAL_CHIP | EN_BCN_FUNCTION | EN_TXBCN_RPT | BIT(1)));
#else
			rtw_write8(Adapter, REG_BCN_CTRL, (DIS_TSF_UDT0_NORMAL_CHIP | EN_BCN_FUNCTION | BIT(1)));
#endif

#ifdef CONFIG_CONCURRENT_MODE
			if (!rtw_mi_buddy_check_fwstate(Adapter, WIFI_FW_ASSOC_SUCCESS))
				rtw_write8(Adapter, REG_BCN_CTRL_1,
					rtw_read8(Adapter, REG_BCN_CTRL_1) & ~EN_BCN_FUNCTION);
#endif

			/* dis BCN1 ATIM  WND if if2 is station */
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1) | BIT(0));
#ifdef CONFIG_TSF_RESET_OFFLOAD
			/* Reset TSF for STA+AP concurrent mode */
			if (rtw_mi_buddy_check_fwstate(Adapter, (WIFI_STATION_STATE | WIFI_ASOC_STATE))) {
				if (reset_tsf(Adapter, HW_PORT0) == false)
					RTW_INFO("ERROR! %s()-%d: Reset port0 TSF fail\n",
						 __func__, __LINE__);
			}
#endif /* CONFIG_TSF_RESET_OFFLOAD */
		}
#endif
	}
}

static void hw_var_set_bcn_func(PADAPTER Adapter, u8 variable, u8 *val)
{
	u32 bcn_ctrl_reg;

#ifdef CONFIG_CONCURRENT_MODE
	if (Adapter->hw_port == HW_PORT1)
		bcn_ctrl_reg = REG_BCN_CTRL_1;
	else
#endif
		bcn_ctrl_reg = REG_BCN_CTRL;


	if (*((u8 *)val))
		rtw_write8(Adapter, bcn_ctrl_reg, (EN_BCN_FUNCTION | EN_TXBCN_RPT));
	else
		rtw_write8(Adapter, bcn_ctrl_reg, rtw_read8(Adapter, bcn_ctrl_reg) & (~(EN_BCN_FUNCTION | EN_TXBCN_RPT)));


}

static void hw_var_set_correct_tsf(PADAPTER Adapter, u8 variable, u8 *val)
{
#ifdef CONFIG_MI_WITH_MBSSID_CAM
	/*do nothing*/
#else
	u64	tsf;
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE)) {
		/* pHalData->RegTxPause |= STOP_BCNQ;BIT(6) */
		/* rtw_write8(Adapter, REG_TXPAUSE, (rtw_read8(Adapter, REG_TXPAUSE)|BIT(6))); */
		StopTxBeacon(Adapter);
	}

	/*tsf = pmlmeext->TSFValue - ((u32)pmlmeext->TSFValue % (pmlmeinfo->bcn_interval*1024)) -1024; //us */
	tsf = pmlmeext->TSFValue - rtw_modular64(pmlmeext->TSFValue, (pmlmeinfo->bcn_interval * 1024)) - 1024; /*us*/

	rtw_hal_correct_tsf(Adapter, Adapter->hw_port, tsf);

#ifdef CONFIG_CONCURRENT_MODE
	/* Update buddy port's TSF if it is SoftAP for beacon TX issue!*/
	if ((pmlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE
	    && rtw_mi_check_status(Adapter, MI_AP_MODE)) {

		struct dvobj_priv *dvobj = adapter_to_dvobj(Adapter);
		int i;
		_adapter *iface;

		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (!iface)
				continue;
			if (iface == Adapter)
				continue;

			if (check_fwstate(&iface->mlmepriv, WIFI_AP_STATE) == true
			    && check_fwstate(&iface->mlmepriv, WIFI_ASOC_STATE) == true
			   ) {
				rtw_hal_correct_tsf(iface, iface->hw_port, tsf);
#ifdef CONFIG_TSF_RESET_OFFLOAD
				if (reset_tsf(iface, iface->hw_port) == false)
					RTW_INFO("%s-[ERROR] "ADPT_FMT" Reset port%d TSF fail\n", __func__, ADPT_ARG(iface), iface->hw_port);
#endif	/* CONFIG_TSF_RESET_OFFLOAD*/
			}
		}
	}
#endif/*CONFIG_CONCURRENT_MODE*/
	if (((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE)) {
		/* pHalData->RegTxPause  &= (~STOP_BCNQ); */
		/* rtw_write8(Adapter, REG_TXPAUSE, (rtw_read8(Adapter, REG_TXPAUSE)&(~BIT(6)))); */
		ResumeTxBeacon(Adapter);
	}
#endif /*CONFIG_MI_WITH_MBSSID_CAM*/
}

static void hw_var_set_mlme_disconnect(PADAPTER Adapter, u8 variable, u8 *val)
{
	/*Set RCR to not to receive data frame when NO LINK state
	rtw_write32(Adapter, REG_RCR, rtw_read32(padapter, REG_RCR) & ~RCR_ADF);
	reject all data frames	*/
#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_check_status(Adapter, MI_LINKED) == false)
#endif
		rtw_write16(Adapter, REG_RXFLTMAP2, 0x00);

#ifdef CONFIG_CONCURRENT_MODE
	if (Adapter->hw_port == HW_PORT1) {
		/*reset TSF1*/
		rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(1));

		/*disable update TSF1*/
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1) | DIS_TSF_UDT);

		/* disable Port1's beacon function*/
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1) & (~BIT(3)));
	} else
#endif
	{
		/*reset TSF*/
		rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(0));

		/*disable update TSF*/
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL) | DIS_TSF_UDT);
	}

}


static void hw_var_set_mlme_sitesurvey(PADAPTER Adapter, u8 variable, u8 *val)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(Adapter);
	u32 value_rcr, rcr_clear_bit;
	u16 value_rxfltmap2;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	int i;
	_adapter *iface;

#ifdef DBG_IFACE_STATUS
	DBG_IFACE_STATUS_DUMP(Adapter);
#endif


#ifdef CONFIG_FIND_BEST_CHANNEL

	rcr_clear_bit = (RCR_CBSSID_BCN | RCR_CBSSID_DATA);

	/* Receive all data frames */
	value_rxfltmap2 = 0xFFFF;

#else /* CONFIG_FIND_BEST_CHANNEL */

	rcr_clear_bit = RCR_CBSSID_BCN;

	/* config RCR to receive different BSSID & not to receive data frame */
	value_rxfltmap2 = 0;

#endif /* CONFIG_FIND_BEST_CHANNEL */

	if (rtw_mi_check_fwstate(Adapter, WIFI_AP_STATE))
		rcr_clear_bit = RCR_CBSSID_BCN;

#ifdef CONFIG_TDLS
	/* TDLS will clear RCR_CBSSID_DATA bit for connection.*/
	else if (Adapter->tdlsinfo.link_established == true)
		rcr_clear_bit = RCR_CBSSID_BCN;
#endif /*CONFIG_TDLS*/

	value_rcr = rtw_read32(Adapter, REG_RCR);

	if (*((u8 *)val)) {/*under sitesurvey*/
		/*
		* 1. configure REG_RXFLTMAP2
		* 2. disable TSF update &  buddy TSF update to avoid updating wrong TSF due to clear RCR_CBSSID_BCN
		* 3. config RCR to receive different BSSID BCN or probe rsp
		*/

		rtw_write16(Adapter, REG_RXFLTMAP2, value_rxfltmap2);

#ifdef CONFIG_MI_WITH_MBSSID_CAM
		/*do nothing~~*/
#else

		/* disable update TSF */
		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (!iface)
				continue;

			if (rtw_linked_check(iface) &&
			    check_fwstate(&(iface->mlmepriv), WIFI_AP_STATE) != true) {
				if (iface->hw_port == HW_PORT1)
					rtw_write8(iface, REG_BCN_CTRL_1, rtw_read8(iface, REG_BCN_CTRL_1) | DIS_TSF_UDT);
				else
					rtw_write8(iface, REG_BCN_CTRL, rtw_read8(iface, REG_BCN_CTRL) | DIS_TSF_UDT);

				iface->mlmeextpriv.en_hw_update_tsf = false;
			}

		}
#endif/*CONFIG_MI_WITH_MBSSID_CAM*/

		value_rcr &= ~(rcr_clear_bit);
		rtw_write32(Adapter, REG_RCR, value_rcr);

		/* Save orignal RRSR setting.*/
		pHalData->RegRRSR = rtw_read16(Adapter, REG_RRSR);

		if (rtw_mi_check_status(Adapter, MI_AP_MODE))
			StopTxBeacon(Adapter);
	} else {/*sitesurvey done*/
		/*
		* 1. enable rx data frame
		* 2. config RCR not to receive different BSSID BCN or probe rsp
		* 3. doesn't enable TSF update &  buddy TSF right now to avoid HW conflict
		*	 so, we enable TSF update when rx first BCN after sitesurvey done
		*/

		if (rtw_mi_check_fwstate(Adapter, _FW_LINKED | WIFI_AP_STATE))
			rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);/*enable to rx data frame*/

#ifdef CONFIG_MI_WITH_MBSSID_CAM
		value_rcr &= ~(RCR_CBSSID_BCN | RCR_CBSSID_DATA);
#else
		value_rcr |= rcr_clear_bit;
#endif
		/* for 11n Logo 4.2.31/4.2.32, disable BSSID BCN check for AP mode */
		if (Adapter->registrypriv.wifi_spec && MLME_IS_AP(Adapter))
			value_rcr &= ~(RCR_CBSSID_BCN);
		
		rtw_write32(Adapter, REG_RCR, value_rcr);

#ifdef CONFIG_MI_WITH_MBSSID_CAM
		/*if ((rtw_mi_get_assoced_sta_num(Adapter) == 1) && (!rtw_mi_check_status(Adapter, MI_AP_MODE)))
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~DIS_TSF_UDT));*/
#else

		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (!iface)
				continue;
			if (rtw_linked_check(iface) &&
			    check_fwstate(&(iface->mlmepriv), WIFI_AP_STATE) != true) {
				/* enable HW TSF update when recive beacon*/
				/*if (iface->hw_port == HW_PORT1)
					rtw_write8(iface, REG_BCN_CTRL_1, rtw_read8(iface, REG_BCN_CTRL_1)&(~(DIS_TSF_UDT)));
				else
					rtw_write8(iface, REG_BCN_CTRL, rtw_read8(iface, REG_BCN_CTRL)&(~(DIS_TSF_UDT)));
				*/
				iface->mlmeextpriv.en_hw_update_tsf = true;
			}
		}
#endif

		/* Restore orignal RRSR setting. */
		rtw_write16(Adapter, REG_RRSR, pHalData->RegRRSR);

		if (rtw_mi_get_ap_num(Adapter)) {
			ResumeTxBeacon(Adapter);
			rtw_mi_tx_beacon_hdl(Adapter);
		}
	}
}

static void hw_var_set_mlme_join(PADAPTER Adapter, u8 variable, u8 *val)
{
#ifdef CONFIG_CONCURRENT_MODE
	u8	RetryLimit = 0x30;
	u8	type = *((u8 *)val);
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;

	if (type == 0) { /* prepare to join */
		if (rtw_mi_check_status(Adapter, MI_AP_MODE))
			StopTxBeacon(Adapter);

		/*enable to rx data frame.Accept all data frame*/
		/*rtw_write32(padapter, REG_RCR, rtw_read32(padapter, REG_RCR)|RCR_ADF);*/
		rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);
#ifdef CONFIG_MI_WITH_MBSSID_CAM
		/*
		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) && (rtw_mi_get_assoced_sta_num(Adapter) == 1))
			rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);
		else if ((rtw_mi_get_ap_num(Adapter) == 1) && (rtw_mi_get_assoced_sta_num(Adapter) == 1))
			rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_BCN);
		else*/
		rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR) & (~(RCR_CBSSID_DATA | RCR_CBSSID_BCN)));
#else
		if (rtw_mi_check_status(Adapter, MI_AP_MODE))
			rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR) | RCR_CBSSID_BCN);
		else
			rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR) | RCR_CBSSID_DATA | RCR_CBSSID_BCN);
#endif
		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true)
			RetryLimit = (pHalData->CustomerID == RT_CID_CCX) ? 7 : 48;
		else /* Ad-hoc Mode */
			RetryLimit = 0x7;
	} else if (type == 1) { /* joinbss_event call back when join res < 0 */
		if (rtw_mi_check_status(Adapter, MI_LINKED) == false)
			rtw_write16(Adapter, REG_RXFLTMAP2, 0x00);

		if (rtw_mi_check_status(Adapter, MI_AP_MODE)) {
			ResumeTxBeacon(Adapter);

			/* reset TSF 1/2 after ResumeTxBeacon */
			rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(1) | BIT(0));

		}
	} else if (type == 2) { /* sta add event call back */
#ifdef CONFIG_MI_WITH_MBSSID_CAM
		/*if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) && (rtw_mi_get_assoced_sta_num(Adapter) == 1))
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~DIS_TSF_UDT));*/
#else
		/* enable update TSF */
		if (Adapter->hw_port == HW_PORT1)
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1) & (~DIS_TSF_UDT));
		else
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL) & (~DIS_TSF_UDT));

#endif
		if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE)) {
			/* fixed beacon issue for 8191su........... */
			rtw_write8(Adapter, 0x542 , 0x02);
			RetryLimit = 0x7;
		}


		if (rtw_mi_check_status(Adapter, MI_AP_MODE)) {
			ResumeTxBeacon(Adapter);

			/* reset TSF 1/2 after ResumeTxBeacon */
			rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(1) | BIT(0));
		}

	}

	rtw_write16(Adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);

#endif
}



void SetHwReg8188E(_adapter *adapter, u8 variable, u8 *val)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT		*podmpriv = &pHalData->odmpriv;

	switch (variable) {

	case HW_VAR_SET_OPMODE:
		hw_var_set_opmode(adapter, variable, val);
		break;
	case HW_VAR_BASIC_RATE: {
		struct mlme_ext_info *mlmext_info = &adapter->mlmeextpriv.mlmext_info;
		u16 input_b = 0, masked = 0, ioted = 0, BrateCfg = 0;
		u16 rrsr_2g_force_mask = RRSR_CCK_RATES;
		u16 rrsr_2g_allow_mask = (RRSR_24M | RRSR_12M | RRSR_6M | RRSR_CCK_RATES);

		HalSetBrateCfg(adapter, val, &BrateCfg);
		input_b = BrateCfg;

		/* apply force and allow mask */
		BrateCfg |= rrsr_2g_force_mask;
		BrateCfg &= rrsr_2g_allow_mask;
		masked = BrateCfg;

		/* IOT consideration */
		if (mlmext_info->assoc_AP_vendor == HT_IOT_PEER_CISCO) {
			/* if peer is cisco and didn't use ofdm rate, we enable 6M ack */
			if ((BrateCfg & (RRSR_24M | RRSR_12M | RRSR_6M)) == 0)
				BrateCfg |= RRSR_6M;
		}
		ioted = BrateCfg;

		pHalData->BasicRateSet = BrateCfg;

		RTW_INFO("HW_VAR_BASIC_RATE: %#x->%#x->%#x\n", input_b, masked, ioted);

		/* Set RRSR rate table. */
		rtw_write16(adapter, REG_RRSR, BrateCfg);
		rtw_write8(adapter, REG_RRSR + 2, rtw_read8(adapter, REG_RRSR + 2) & 0xf0);

		rtw_hal_set_hwreg(adapter, HW_VAR_INIT_RTS_RATE, (u8 *)&BrateCfg);
	}
	break;
	case HW_VAR_TXPAUSE:
		rtw_write8(adapter, REG_TXPAUSE, *((u8 *)val));
		break;
	case HW_VAR_BCN_FUNC:
		hw_var_set_bcn_func(adapter, variable, val);
		break;

	case HW_VAR_CORRECT_TSF:
		hw_var_set_correct_tsf(adapter, variable, val);
		break;

	case HW_VAR_CHECK_BSSID:
		if (*((u8 *)val))
			rtw_write32(adapter, REG_RCR, rtw_read32(adapter, REG_RCR) | RCR_CBSSID_DATA | RCR_CBSSID_BCN);
		else {
			u32	val32;

			val32 = rtw_read32(adapter, REG_RCR);

			val32 &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN);

			rtw_write32(adapter, REG_RCR, val32);
		}
		break;

	case HW_VAR_MLME_DISCONNECT:
		hw_var_set_mlme_disconnect(adapter, variable, val);
		break;

	case HW_VAR_MLME_SITESURVEY:
		hw_var_set_mlme_sitesurvey(adapter, variable,  val);
		break;

	case HW_VAR_MLME_JOIN:
#ifdef CONFIG_CONCURRENT_MODE
		hw_var_set_mlme_join(adapter, variable,  val);
#else
		{
			u8	RetryLimit = 0x30;
			u8	type = *((u8 *)val);
			struct mlme_priv	*pmlmepriv = &adapter->mlmepriv;

			if (type == 0) { /* prepare to join */
				/* enable to rx data frame.Accept all data frame */
				/* rtw_write32(padapter, REG_RCR, rtw_read32(padapter, REG_RCR)|RCR_ADF); */
				rtw_write16(adapter, REG_RXFLTMAP2, 0xFFFF);

				if (adapter->in_cta_test) {
					u32 v = rtw_read32(adapter, REG_RCR);
					v &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN); /* | RCR_ADF */
					rtw_write32(adapter, REG_RCR, v);
				} else
					rtw_write32(adapter, REG_RCR, rtw_read32(adapter, REG_RCR) | RCR_CBSSID_DATA | RCR_CBSSID_BCN);

				if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true)
					RetryLimit = (pHalData->CustomerID == RT_CID_CCX) ? 7 : 48;
				else /* Ad-hoc Mode */
					RetryLimit = 0x7;
			} else if (type == 1) /* joinbss_event call back when join res < 0 */
				rtw_write16(adapter, REG_RXFLTMAP2, 0x00);
			else if (type == 2) { /* sta add event call back */
				/* enable update TSF */
				rtw_write8(adapter, REG_BCN_CTRL, rtw_read8(adapter, REG_BCN_CTRL) & (~BIT(4)));

				if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE))
					RetryLimit = 0x7;
			}

			rtw_write16(adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);
		}
#endif
		break;

	case HW_VAR_ON_RCR_AM:
		rtw_write32(adapter, REG_RCR, rtw_read32(adapter, REG_RCR) | RCR_AM);
		RTW_INFO("%s, %d, RCR= %x\n", __func__, __LINE__, rtw_read32(adapter, REG_RCR));
		break;
	case HW_VAR_OFF_RCR_AM:
		rtw_write32(adapter, REG_RCR, rtw_read32(adapter, REG_RCR) & (~RCR_AM));
		RTW_INFO("%s, %d, RCR= %x\n", __func__, __LINE__, rtw_read32(adapter, REG_RCR));
		break;
	case HW_VAR_BEACON_INTERVAL:
		rtw_write16(adapter, REG_BCN_INTERVAL, *((u16 *)val));
#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
		{
			struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
			struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
			u16 bcn_interval =	*((u16 *)val);
			if ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) {
				RTW_INFO("%s==> bcn_interval:%d, eraly_int:%d\n", __func__, bcn_interval, bcn_interval >> 1);
				rtw_write8(adapter, REG_DRVERLYINT, bcn_interval >> 1); /* 50ms for sdio */
			}
		}
#endif/* CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */

		break;
	case HW_VAR_SLOT_TIME: {
		rtw_write8(adapter, REG_SLOT, val[0]);
	}
	break;
	case HW_VAR_ACK_PREAMBLE: {
		u8	regTmp;
		u8	bShortPreamble = *((bool *)val);
		/* Joseph marked out for Netgear 3500 TKIP channel 7 issue.(Temporarily) */
		regTmp = (pHalData->nCur40MhzPrimeSC) << 5;
		rtw_write8(adapter, REG_RRSR + 2, regTmp);

		regTmp = rtw_read8(adapter, REG_WMAC_TRXPTCL_CTL + 2);
		if (bShortPreamble)
			regTmp |= BIT1;
		else
			regTmp &= (~BIT1);
		rtw_write8(adapter, REG_WMAC_TRXPTCL_CTL + 2, regTmp);
	}
	break;
	case HW_VAR_CAM_EMPTY_ENTRY: {
		u8	ucIndex = *((u8 *)val);
		u8	i;
		u32	ulCommand = 0;
		u32	ulContent = 0;
		u32	ulEncAlgo = CAM_AES;

		for (i = 0; i < CAM_CONTENT_COUNT; i++) {
			/* filled id in CAM config 2 byte */
			if (i == 0) {
				ulContent |= (ucIndex & 0x03) | ((u16)(ulEncAlgo) << 2);
				/* ulContent |= CAM_VALID; */
			} else
				ulContent = 0;
			/* polling bit, and No Write enable, and address */
			ulCommand = CAM_CONTENT_COUNT * ucIndex + i;
			ulCommand = ulCommand | CAM_POLLINIG | CAM_WRITE;
			/* write content 0 is equall to mark invalid */
			rtw_write32(adapter, WCAMI, ulContent);  /* delay_ms(40); */
			rtw_write32(adapter, RWCAM, ulCommand);  /* delay_ms(40); */
		}
	}
	break;
	case HW_VAR_CAM_INVALID_ALL:
		rtw_write32(adapter, RWCAM, BIT(31) | BIT(30));
		break;
	case HW_VAR_AC_PARAM_VO:
		rtw_write32(adapter, REG_EDCA_VO_PARAM, ((u32 *)(val))[0]);
		break;
	case HW_VAR_AC_PARAM_VI:
		rtw_write32(adapter, REG_EDCA_VI_PARAM, ((u32 *)(val))[0]);
		break;
	case HW_VAR_AC_PARAM_BE:
		pHalData->ac_param_be = ((u32 *)(val))[0];
		rtw_write32(adapter, REG_EDCA_BE_PARAM, ((u32 *)(val))[0]);
		break;
	case HW_VAR_AC_PARAM_BK:
		rtw_write32(adapter, REG_EDCA_BK_PARAM, ((u32 *)(val))[0]);
		break;
	case HW_VAR_ACM_CTRL: {
		u8	acm_ctrl = *((u8 *)val);
		u8	AcmCtrl = rtw_read8(adapter, REG_ACMHWCTRL);

		if (acm_ctrl > 1)
			AcmCtrl = AcmCtrl | 0x1;

		if (acm_ctrl & BIT(3))
			AcmCtrl |= AcmHw_VoqEn;
		else
			AcmCtrl &= (~AcmHw_VoqEn);

		if (acm_ctrl & BIT(2))
			AcmCtrl |= AcmHw_ViqEn;
		else
			AcmCtrl &= (~AcmHw_ViqEn);

		if (acm_ctrl & BIT(1))
			AcmCtrl |= AcmHw_BeqEn;
		else
			AcmCtrl &= (~AcmHw_BeqEn);

		RTW_INFO("[HW_VAR_ACM_CTRL] Write 0x%X\n", AcmCtrl);
		rtw_write8(adapter, REG_ACMHWCTRL, AcmCtrl);
	}
	break;
	case HW_VAR_AMPDU_FACTOR: {
		u8	RegToSet_Normal[4] = {0x41, 0xa8, 0x72, 0xb9};
		u8	RegToSet_BT[4] = {0x31, 0x74, 0x42, 0x97};
		u8	FactorToSet;
		u8	*pRegToSet;
		u8	index = 0;

#ifdef CONFIG_BT_COEXIST
		if ((pHalData->bt_coexist.BT_Coexist) &&
		    (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC4))
			pRegToSet = RegToSet_BT; /* 0x97427431; */
		else
#endif
			pRegToSet = RegToSet_Normal; /* 0xb972a841; */

		FactorToSet = *((u8 *)val);
		if (FactorToSet <= 3) {
			FactorToSet = (1 << (FactorToSet + 2));
			if (FactorToSet > 0xf)
				FactorToSet = 0xf;

			for (index = 0; index < 4; index++) {
				if ((pRegToSet[index] & 0xf0) > (FactorToSet << 4))
					pRegToSet[index] = (pRegToSet[index] & 0x0f) | (FactorToSet << 4);

				if ((pRegToSet[index] & 0x0f) > FactorToSet)
					pRegToSet[index] = (pRegToSet[index] & 0xf0) | (FactorToSet);

				rtw_write8(adapter, (REG_AGGLEN_LMT + index), pRegToSet[index]);
			}

		}
	}
	break;
	case HW_VAR_H2C_FW_PWRMODE: {
		u8	psmode = (*(u8 *)val);

		/* Forece leave RF low power mode for 1T1R to prevent conficting setting in Fw power */
		/* saving sequence. 2010.06.07. Added by tynli. Suggested by SD3 yschang. */
		if (psmode != PS_MODE_ACTIVE)
			odm_rf_saving(podmpriv, true);
		rtl8188e_set_FwPwrMode_cmd(adapter, psmode);
	}
	break;
	case HW_VAR_H2C_FW_JOINBSSRPT: {
		u8	mstatus = (*(u8 *)val);
		rtl8188e_set_FwJoinBssReport_cmd(adapter, mstatus);
	}
	break;
#ifdef CONFIG_P2P_PS
	case HW_VAR_H2C_FW_P2P_PS_OFFLOAD: {
		u8	p2p_ps_state = (*(u8 *)val);
		rtl8188e_set_p2p_ps_offload_cmd(adapter, p2p_ps_state);
	}
	break;
#endif /* CONFIG_P2P_PS */
#ifdef CONFIG_TDLS
	case HW_VAR_TDLS_WRCR:
		rtw_write32(adapter, REG_RCR, rtw_read32(adapter, REG_RCR) & (~RCR_CBSSID_DATA));
		break;
	case HW_VAR_TDLS_RS_RCR:
		rtw_write32(adapter, REG_RCR, rtw_read32(adapter, REG_RCR) | (RCR_CBSSID_DATA));
		break;
#endif /* CONFIG_TDLS */
#ifdef CONFIG_BT_COEXIST
	case HW_VAR_BT_SET_COEXIST: {
		u8	bStart = (*(u8 *)val);
		rtl8192c_set_dm_bt_coexist(adapter, bStart);
	}
	break;
	case HW_VAR_BT_ISSUE_DELBA: {
		u8	dir = (*(u8 *)val);
		rtl8192c_issue_delete_ba(adapter, dir);
	}
	break;
#endif
#if (RATE_ADAPTIVE_SUPPORT == 1)
	case HW_VAR_RPT_TIMER_SETTING: {
		u16	min_rpt_time = (*(u16 *)val);

		odm_ra_set_tx_rpt_time(podmpriv, min_rpt_time);
	}
	break;
#endif

	case HW_VAR_EFUSE_BYTES: /* To set EFUE total used bytes, added by Roger, 2008.12.22. */
		pHalData->EfuseUsedBytes = *((u16 *)val);
		break;
	case HW_VAR_FIFO_CLEARN_UP: {
		struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
		u8 trycnt = 100;

		/* pause tx */
		rtw_write8(adapter, REG_TXPAUSE, 0xff);

		/* keep sn */
		adapter->xmitpriv.nqos_ssn = rtw_read16(adapter, REG_NQOS_SEQ);

		if (pwrpriv->bkeepfwalive != true) {
			/* RX DMA stop */
			rtw_write32(adapter, REG_RXPKT_NUM, (rtw_read32(adapter, REG_RXPKT_NUM) | RW_RELEASE_EN));
			do {
				if (!(rtw_read32(adapter, REG_RXPKT_NUM) & RXDMA_IDLE))
					break;
			} while (trycnt--);
			if (trycnt == 0)
				RTW_INFO("Stop RX DMA failed......\n");

			/* RQPN Load 0 */
			rtw_write16(adapter, REG_RQPN_NPQ, 0x0);
			rtw_write32(adapter, REG_RQPN, 0x80000000);
			rtw_mdelay_os(10);
		}
	}
	break;

	case HW_VAR_RESTORE_HW_SEQ:
		/* restore Sequence No. */
		rtw_write8(adapter, 0x4dc, adapter->xmitpriv.nqos_ssn);
		break;

#if (RATE_ADAPTIVE_SUPPORT == 1)
	case HW_VAR_TX_RPT_MAX_MACID: {
		u8 maxMacid = *val;

		RTW_INFO("### MacID(%d),Set Max Tx RPT MID(%d)\n", maxMacid, maxMacid + 1);
		rtw_write8(adapter, REG_TX_RPT_CTRL + 1, maxMacid + 1);
	}
	break;
#endif /* (RATE_ADAPTIVE_SUPPORT == 1) */

	case HW_VAR_BCN_VALID:
		/* BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2, write 1 to clear, Clear by sw */
		rtw_write8(adapter, REG_TDECTRL + 2, rtw_read8(adapter, REG_TDECTRL + 2) | BIT0);
		break;


	case HW_VAR_CHECK_TXBUF: {
		u8 retry_limit;
		u16 val16;
		u32 reg_200 = 0, reg_204 = 0;
		u32 init_reg_200 = 0, init_reg_204 = 0;
		u32 start = rtw_get_current_time();
		u32 pass_ms;
		int i = 0;

		retry_limit = 0x01;

		val16 = retry_limit << RETRY_LIMIT_SHORT_SHIFT | retry_limit << RETRY_LIMIT_LONG_SHIFT;
		rtw_write16(adapter, REG_RL, val16);

		while (rtw_get_passing_time_ms(start) < 2000
		       && !RTW_CANNOT_RUN(adapter)
		      ) {
			reg_200 = rtw_read32(adapter, 0x200);
			reg_204 = rtw_read32(adapter, 0x204);

			if (i == 0) {
				init_reg_200 = reg_200;
				init_reg_204 = reg_204;
			}

			i++;
			if ((reg_200 & 0x00ffffff) != (reg_204 & 0x00ffffff)) {
				/* RTW_INFO("%s: (HW_VAR_CHECK_TXBUF)TXBUF NOT empty - 0x204=0x%x, 0x200=0x%x (%d)\n", __func__, reg_204, reg_200, i); */
				rtw_msleep_os(10);
			} else
				break;
		}

		pass_ms = rtw_get_passing_time_ms(start);

		if (RTW_CANNOT_RUN(adapter))
			;
		else if (pass_ms >= 2000 || (reg_200 & 0x00ffffff) != (reg_204 & 0x00ffffff)) {
			RTW_PRINT("%s:(HW_VAR_CHECK_TXBUF)NOT empty(%d) in %d ms\n", __func__, i, pass_ms);
			RTW_PRINT("%s:(HW_VAR_CHECK_TXBUF)0x200=0x%08x, 0x204=0x%08x (0x%08x, 0x%08x)\n",
				__func__, reg_200, reg_204, init_reg_200, init_reg_204);
			/* rtw_warn_on(1); */
		} else
			RTW_INFO("%s:(HW_VAR_CHECK_TXBUF)TXBUF Empty(%d) in %d ms\n", __func__, i, pass_ms);

		retry_limit = 0x30;
		val16 = retry_limit << RETRY_LIMIT_SHORT_SHIFT | retry_limit << RETRY_LIMIT_LONG_SHIFT;
		rtw_write16(adapter, REG_RL, val16);
	}
	break;
	case HW_VAR_RESP_SIFS: {
		struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;

		if ((pmlmeext->cur_wireless_mode == WIRELESS_11G) ||
		    (pmlmeext->cur_wireless_mode == WIRELESS_11BG)) { /* WIRELESS_MODE_G){ */
			val[0] = 0x0a;
			val[1] = 0x0a;
		} else {
			val[0] = 0x0e;
			val[1] = 0x0e;
		}

		/* SIFS for OFDM Data ACK */
		rtw_write8(adapter, REG_SIFS_CTX + 1, val[0]);
		/* SIFS for OFDM consecutive tx like CTS data! */
		rtw_write8(adapter, REG_SIFS_TRX + 1, val[1]);

		rtw_write8(adapter, REG_SPEC_SIFS + 1, val[0]);
		rtw_write8(adapter, REG_MAC_SPEC_SIFS + 1, val[0]);

		/* RESP_SIFS for OFDM */
		rtw_write8(adapter, REG_RESP_SIFS_OFDM, val[0]);
		rtw_write8(adapter, REG_RESP_SIFS_OFDM + 1, val[0]);
	}
	break;

	case HW_VAR_MACID_LINK: {
		u32 reg_macid_no_link;
		u8 bit_shift;
		u8 id = *(u8 *)val;
		u32 val32;

		if (id < 32) {
			reg_macid_no_link = REG_MACID_NO_LINK_0;
			bit_shift = id;
		} else if (id < 64) {
			reg_macid_no_link = REG_MACID_NO_LINK_1;
			bit_shift = id - 32;
		} else {
			rtw_warn_on(1);
			break;
		}

		val32 = rtw_read32(adapter, reg_macid_no_link);
		if (!(val32 & BIT(bit_shift)))
			break;

		val32 &= ~BIT(bit_shift);
		rtw_write32(adapter, reg_macid_no_link, val32);
	}
	break;

	case HW_VAR_MACID_NOLINK: {
		u32 reg_macid_no_link;
		u8 bit_shift;
		u8 id = *(u8 *)val;
		u32 val32;

		if (id < 32) {
			reg_macid_no_link = REG_MACID_NO_LINK_0;
			bit_shift = id;
		} else if (id < 64) {
			reg_macid_no_link = REG_MACID_NO_LINK_1;
			bit_shift = id - 32;
		} else {
			rtw_warn_on(1);
			break;
		}

		val32 = rtw_read32(adapter, reg_macid_no_link);
		if (val32 & BIT(bit_shift))
			break;

		val32 |= BIT(bit_shift);
		rtw_write32(adapter, reg_macid_no_link, val32);
	}
	break;

	case HW_VAR_MACID_SLEEP: {
		u32 reg_macid_sleep;
		u8 bit_shift;
		u8 id = *(u8 *)val;
		u32 val32;

		if (id < 32) {
			reg_macid_sleep = REG_MACID_PAUSE_0;
			bit_shift = id;
		} else if (id < 64) {
			reg_macid_sleep = REG_MACID_PAUSE_1;
			bit_shift = id - 32;
		} else {
			rtw_warn_on(1);
			break;
		}

		val32 = rtw_read32(adapter, reg_macid_sleep);
		RTW_INFO(FUNC_ADPT_FMT ": [HW_VAR_MACID_SLEEP] macid=%d, org reg_0x%03x=0x%08X\n",
			FUNC_ADPT_ARG(adapter), id, reg_macid_sleep, val32);

		if (val32 & BIT(bit_shift))
			break;

		val32 |= BIT(bit_shift);
		rtw_write32(adapter, reg_macid_sleep, val32);
	}
	break;

	case HW_VAR_MACID_WAKEUP: {
		u32 reg_macid_sleep;
		u8 bit_shift;
		u8 id = *(u8 *)val;
		u32 val32;

		if (id < 32) {
			reg_macid_sleep = REG_MACID_PAUSE_0;
			bit_shift = id;
		} else if (id < 64) {
			reg_macid_sleep = REG_MACID_PAUSE_1;
			bit_shift = id - 32;
		} else {
			rtw_warn_on(1);
			break;
		}

		val32 = rtw_read32(adapter, reg_macid_sleep);
		RTW_INFO(FUNC_ADPT_FMT ": [HW_VAR_MACID_WAKEUP] macid=%d, org reg_0x%03x=0x%08X\n",
			FUNC_ADPT_ARG(adapter), id, reg_macid_sleep, val32);

		if (!(val32 & BIT(bit_shift)))
			break;

		val32 &= ~BIT(bit_shift);
		rtw_write32(adapter, reg_macid_sleep, val32);
	}
	break;

	default:
		SetHwReg(adapter, variable, val);
		break;
	}

}

struct qinfo_88e {
	u32 head:8;
	u32 pkt_num:8;
	u32 tail:8;
	u32 ac:2;
	u32 macid:6;
};

struct bcn_qinfo_88e {
	u16 head:8;
	u16 pkt_num:8;
};

static void dump_qinfo_88e(void *sel, struct qinfo_88e *info, const char *tag)
{
	/* if (info->pkt_num) */
	RTW_PRINT_SEL(sel, "%shead:0x%02x, tail:0x%02x, pkt_num:%u, macid:%u, ac:%u\n"
		, tag ? tag : "", info->head, info->tail, info->pkt_num, info->macid, info->ac
		     );
}

static void dump_bcn_qinfo_88e(void *sel, struct bcn_qinfo_88e *info, const char *tag)
{
	/* if (info->pkt_num) */
	RTW_PRINT_SEL(sel, "%shead:0x%02x, pkt_num:%u\n"
		      , tag ? tag : "", info->head, info->pkt_num
		     );
}

static void dump_mac_qinfo_88e(void *sel, _adapter *adapter)
{
	u32 q0_info;
	u32 q1_info;
	u32 q2_info;
	u32 q3_info;
	/*
	u32 q4_info;
	u32 q5_info;
	u32 q6_info;
	u32 q7_info;
	*/
	u32 mg_q_info;
	u32 hi_q_info;
	u16 bcn_q_info;

	q0_info = rtw_read32(adapter, REG_Q0_INFO);
	q1_info = rtw_read32(adapter, REG_Q1_INFO);
	q2_info = rtw_read32(adapter, REG_Q2_INFO);
	q3_info = rtw_read32(adapter, REG_Q3_INFO);
	/*
	q4_info = rtw_read32(adapter, REG_Q4_INFO);
	q5_info = rtw_read32(adapter, REG_Q5_INFO);
	q6_info = rtw_read32(adapter, REG_Q6_INFO);
	q7_info = rtw_read32(adapter, REG_Q7_INFO);
	*/
	mg_q_info = rtw_read32(adapter, REG_MGQ_INFO);
	hi_q_info = rtw_read32(adapter, REG_HGQ_INFO);
	bcn_q_info = rtw_read16(adapter, REG_BCNQ_INFO);

	dump_qinfo_88e(sel, (struct qinfo_88e *)&q0_info, "Q0 ");
	dump_qinfo_88e(sel, (struct qinfo_88e *)&q1_info, "Q1 ");
	dump_qinfo_88e(sel, (struct qinfo_88e *)&q2_info, "Q2 ");
	dump_qinfo_88e(sel, (struct qinfo_88e *)&q3_info, "Q3 ");
	/*
	dump_qinfo_88e(sel, (struct qinfo_88e *)&q4_info, "Q4 ");
	dump_qinfo_88e(sel, (struct qinfo_88e *)&q5_info, "Q5 ");
	dump_qinfo_88e(sel, (struct qinfo_88e *)&q6_info, "Q6 ");
	dump_qinfo_88e(sel, (struct qinfo_88e *)&q7_info, "Q7 ");
	*/
	dump_qinfo_88e(sel, (struct qinfo_88e *)&mg_q_info, "MG ");
	dump_qinfo_88e(sel, (struct qinfo_88e *)&hi_q_info, "HI ");
	dump_bcn_qinfo_88e(sel, (struct bcn_qinfo_88e *)&bcn_q_info, "BCN ");
}

void GetHwReg8188E(_adapter *adapter, u8 variable, u8 *val)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);


	switch (variable) {
	case HW_VAR_SYS_CLKR:
		*val = rtw_read8(adapter, REG_SYS_CLKR);
		break;

	case HW_VAR_TXPAUSE:
		val[0] = rtw_read8(adapter, REG_TXPAUSE);
		break;
	case HW_VAR_BCN_VALID:
		/* BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2 */
		val[0] = (BIT0 & rtw_read8(adapter, REG_TDECTRL + 2)) ? true : false;
		break;
	case HW_VAR_FWLPS_RF_ON: {
		/* When we halt NIC, we should check if FW LPS is leave. */
		if (adapter_to_pwrctl(adapter)->rf_pwrstate == rf_off) {
			/* If it is in HW/SW Radio OFF or IPS state, we do not check Fw LPS Leave, */
			/* because Fw is unload. */
			val[0] = true;
		} else {
			u32 valRCR;
			valRCR = rtw_read32(adapter, REG_RCR);
			valRCR &= 0x00070000;
			if (valRCR)
				val[0] = false;
			else
				val[0] = true;
		}
	}
	break;
	case HW_VAR_EFUSE_BYTES: /* To get EFUE total used bytes, added by Roger, 2008.12.22. */
		*((u16 *)(val)) = pHalData->EfuseUsedBytes;
		break;
	case HW_VAR_CHK_HI_QUEUE_EMPTY:
		*val = ((rtw_read32(adapter, REG_HGQ_INFO) & 0x0000ff00) == 0) ? true : false;
		break;
	case HW_VAR_DUMP_MAC_QUEUE_INFO:
		dump_mac_qinfo_88e(val, adapter);
		break;
	default:
		GetHwReg(adapter, variable, val);
		break;
	}

}

static void hal_ra_info_dump(_adapter *padapter , void *sel)
{
	int i;
	u8 mac_id;
	u8 bLinked = false;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	_adapter *iface;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if ((iface) && rtw_is_adapter_up(iface)) {
			if (rtw_linked_check(iface)) {
				bLinked = true;
				break;
			}
		}
	}

	for (i = 0; i < macid_ctl->num; i++) {

		if (rtw_macid_is_used(macid_ctl, i) && !rtw_macid_is_bmc(macid_ctl, i)) {

			mac_id = (u8) i;

			if (bLinked) {
				_RTW_PRINT_SEL(sel , "============ RA status - Mac_id:%d ===================\n", mac_id);
				if (pHalData->fw_ractrl == false) {
#if (RATE_ADAPTIVE_SUPPORT == 1)
					_RTW_PRINT_SEL(sel , "Mac_id:%d ,RSSI:%d(%%)\n", mac_id, pHalData->odmpriv.ra_info[mac_id].rssi_sta_ra);

					_RTW_PRINT_SEL(sel , "rate_sgi = %d, decision_rate = %s\n", pHalData->odmpriv.ra_info[mac_id].rate_sgi,
						HDATA_RATE(pHalData->odmpriv.ra_info[mac_id].decision_rate));

					_RTW_PRINT_SEL(sel , "pt_stage = %d\n", pHalData->odmpriv.ra_info[mac_id].pt_stage);

					_RTW_PRINT_SEL(sel , "rate_id = %d,ra_use_rate = 0x%08x\n", pHalData->odmpriv.ra_info[mac_id].rate_id, pHalData->odmpriv.ra_info[mac_id].ra_use_rate);

#endif /* (RATE_ADAPTIVE_SUPPORT == 1)*/
				} else {
					u8 cur_rate = rtw_read8(padapter, REG_ADAPTIVE_DATA_RATE_0 + mac_id);
					u8 sgi = (cur_rate & BIT7) ? true : false;

					cur_rate &= 0x7f;

					_RTW_PRINT_SEL(sel , "Mac_id:%d ,SGI:%d ,Rate:%s\n", mac_id, sgi, HDATA_RATE(cur_rate));
				}
			}
		}
	}
}

u8
GetHalDefVar8188E(
	PADAPTER				Adapter,
	HAL_DEF_VARIABLE		eVariable,
	void *					pValue
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _SUCCESS;

	switch (eVariable) {
	case HAL_DEF_IS_SUPPORT_ANT_DIV:
#ifdef CONFIG_ANTENNA_DIVERSITY
		*((u8 *)pValue) = (pHalData->AntDivCfg == 0) ? false : true;
#endif
		break;
	case HAL_DEF_DRVINFO_SZ:
		*((u32 *)pValue) = DRVINFO_SZ;
		break;
	case HAL_DEF_MAX_RECVBUF_SZ:
		*((u32 *)pValue) = MAX_RECVBUF_SZ;
		break;
	case HAL_DEF_RX_PACKET_OFFSET:
		*((u32 *)pValue) = RXDESC_SIZE + DRVINFO_SZ * 8;
		break;
#if (RATE_ADAPTIVE_SUPPORT == 1)
	case HAL_DEF_RA_DECISION_RATE: {
		u8 MacID = *((u8 *)pValue);
		*((u8 *)pValue) = odm_ra_get_decision_rate_8188e(&(pHalData->odmpriv), MacID);
	}
	break;

	case HAL_DEF_RA_SGI: {
		u8 MacID = *((u8 *)pValue);
		*((u8 *)pValue) = odm_ra_get_sgi_8188e(&(pHalData->odmpriv), MacID);
	}
	break;
#endif


	case HAL_DEF_PT_PWR_STATUS:
#if (POWER_TRAINING_ACTIVE == 1)
		{
			u8 MacID = *((u8 *)pValue);
			*((u8 *)pValue) = odm_ra_get_hw_pwr_status_8188e(&(pHalData->odmpriv), MacID);
		}
#endif /* (POWER_TRAINING_ACTIVE==1) */
		break;
	case HAL_DEF_EXPLICIT_BEAMFORMEE:
	case HAL_DEF_EXPLICIT_BEAMFORMER:
		*((u8 *)pValue) = false;
		break;

	case HW_DEF_RA_INFO_DUMP:
		hal_ra_info_dump(Adapter, pValue);
		break;

	case HAL_DEF_TX_PAGE_SIZE:
		*((u32 *)pValue) = PAGE_SIZE_128;
		break;
	case HAL_DEF_TX_PAGE_BOUNDARY:
		if (!Adapter->registrypriv.wifi_spec)
			*(u8 *)pValue = TX_PAGE_BOUNDARY_88E(Adapter);
		else
			*(u8 *)pValue = WMM_NORMAL_TX_PAGE_BOUNDARY_88E(Adapter);
		break;
	case HAL_DEF_MACID_SLEEP:
		*(u8 *)pValue = true; /* support macid sleep */
		break;
	case HAL_DEF_RX_DMA_SZ_WOW:
		*(u32 *)pValue = RX_DMA_SIZE_88E(Adapter) - RESV_FMWF;
		break;
	case HAL_DEF_RX_DMA_SZ:
		*(u32 *)pValue = MAX_RX_DMA_BUFFER_SIZE_88E(Adapter);
		break;
	case HAL_DEF_RX_PAGE_SIZE:
		*(u32 *)pValue = PAGE_SIZE_128;
		break;
	case HW_VAR_BEST_AMPDU_DENSITY:
		*((u32 *)pValue) = AMPDU_DENSITY_VALUE_7;
		break;
	default:
		bResult = GetHalDefVar(Adapter, eVariable, pValue);
		break;
	}

	return bResult;
}

#ifdef CONFIG_GPIO_API
int rtl8188e_GpioFuncCheck(PADAPTER adapter, u8 gpio_num)
{
        int ret = _SUCCESS;

        if (IS_HARDWARE_TYPE_8188E(adapter) == _FAIL) {
                if ((gpio_num > 7) || (gpio_num < 4)) {
                        RTW_INFO("%s The gpio number does not included 4~7.\n",__func__);
                        ret = _FAIL;
                }
        }

        return ret;
}
#endif
