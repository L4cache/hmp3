/* ***** BEGIN LICENSE BLOCK *****  
 * Source last modified: 2022-12-07, Maik Merten
 *   
 * Portions Copyright (c) 1995-2005 RealNetworks, Inc. All Rights Reserved.  
 *       
 * The contents of this file, and the files included with this file, 
 * are subject to the current version of the RealNetworks Public 
 * Source License (the "RPSL") available at 
 * http://www.helixcommunity.org/content/rpsl unless you have licensed 
 * the file under the current version of the RealNetworks Community 
 * Source License (the "RCSL") available at 
 * http://www.helixcommunity.org/content/rcsl, in which case the RCSL 
 * will apply. You may also obtain the license terms directly from 
 * RealNetworks.  You may not use this file except in compliance with 
 * the RPSL or, if you have a valid RCSL with RealNetworks applicable 
 * to this file, the RCSL.  Please see the applicable RPSL or RCSL for 
 * the rights, obligations and limitations governing use of the 
 * contents of the file. 
 *   
 * This file is part of the Helix DNA Technology. RealNetworks is the 
 * developer of the Original Code and owns the copyrights in the 
 * portions it created. 
 *   
 * This file, and the files included with this file, is distributed 
 * and made available on an 'AS IS' basis, WITHOUT WARRANTY OF ANY 
 * KIND, EITHER EXPRESS OR IMPLIED, AND REALNETWORKS HEREBY DISCLAIMS 
 * ALL SUCH WARRANTIES, INCLUDING WITHOUT LIMITATION, ANY WARRANTIES 
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, QUIET 
 * ENJOYMENT OR NON-INFRINGEMENT. 
 *  
 * Technology Compatibility Kit Test Suite(s) Location:  
 *    http://www.helixcommunity.org/content/tck  
 *  
 * Contributor(s):  
 *   
 * ***** END LICENSE BLOCK ***** */

#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <math.h>
#include <assert.h>
#include "xhead.h"
#include "hxtypes.h"

static const char tag_vbr[] = { "Xing" };
static const char tag_cbr[] = { "Info" };
static const char tag_shortversion[] = { "LAMEH5.20" };


// 4   Xing
// 4   flags
// 4   frames
// 4   bytes
// 100 toc
// 4   vbr scale

static const int sr_table[6] = {
    22050, 24000, 16000,
    44100, 48000, 32000,
};

static const int br_table[2][16] = {
    0, 8, 16, 24, 32, 40, 48, 56,       // mpeg2
    64, 80, 96, 112, 128, 144, 160, 0,
    0, 32, 40, 48, 56, 64, 80, 96,      // mpeg1
    112, 128, 160, 192, 224, 256, 320, 0,
};

#define N 512
static int table[N + 1][2];
static int toc_ptr;
static int toc_n;

/*-------------------------------------------------------------*/
static void
InsertString ( unsigned char *buf, const char *str, int n )
{
    for( int i = 0; i < n; i++ )
    {
        buf[i] = str[i];
    }
}

/*-------------------------------------------------------------*/
static int
BufStringEqual ( unsigned char *buf, const char *str, int n )
{
    for( int i = 0; i < n; i++ )
    {
        if(buf[i] != str[i]) return 0;
    }
    return 1;
}


/*-------------------------------------------------------------*/
static void
InsertI4 ( unsigned char *buf, int x )
{
// big endian insert

    buf[0] = ( ( unsigned int ) x ) >> 24;
    buf[1] = ( ( unsigned int ) x ) >> 16;
    buf[2] = ( ( unsigned int ) x ) >> 8;
    buf[3] = ( ( unsigned int ) x );

}

/*-------------------------------------------------------------*/
static int
ExtractI4 ( unsigned char *buf )
{
    int x;

// big endian extract

    x = buf[0];
    x <<= 8;
    x |= buf[1];
    x <<= 8;
    x |= buf[2];
    x <<= 8;
    x |= buf[3];

    return x;
}

/*-------------------------------------------------------------*/
static void
BuildTOC ( int tot_frames, int tot_bytes, unsigned char *buf )
{
    int i, k;
    int target, target0, target0b;
    double a, b;
    int index;

    if ( ( tot_frames <= 0 ) || ( tot_bytes <= 0 ) )
    {
        for ( i = 0; i < 100; i++ )
            buf[i] = 0;
        return;
    }

    table[toc_ptr][0] = tot_frames;
    table[toc_ptr][1] = tot_bytes;
    toc_ptr++;

    for ( i = 0; i < toc_ptr; i++ )
    {
        table[i][0] = 100 * table[i][0];
    }

    a = 256.0 / tot_bytes;
    target = 0;
    target0 = 0;
    target0b = 0;
    k = 0;
    for ( i = 0; i < 100; i++ )
    {
        while ( table[k][0] <= target )
        {
            target0 = table[k][0];
            target0b = table[k][1];
            k++;
        }
        assert ( ( table[k][0] - target0 ) > 0 );
        b = target0b +
            ( ( double ) ( target - target0 ) ) *
            ( ( double ) ( table[k][1] - target0b ) ) /
            ( ( double ) ( table[k][0] - target0 ) );
        index = ( int ) ( a * b + 0.5 );
        index = HX_MAX ( index, 0 );       // should not happen
        index = HX_MIN ( index, 255 );     // should not happen
        buf[i] = index;
        target += tot_frames;
    }

//for(i=0;i<100;i++) {
//    if( (i%10) == 0 ) printf("\n");
//    printf(" %3d", (int)buf[i]);
//}

}

/*-------------------------------------------------------------*/
int
XingHeaderBitrateIndex ( int h_mode, int bitrate )
{
    int brIndex;
    h_mode &= 0x1; // allow only 0 and 1

    for( brIndex = 1; brIndex < 15; brIndex++ )
    {
        if( br_table[h_mode][brIndex] == bitrate )
        {
            return brIndex; // found bitrate index
        }
    }

    return 0; // didn't find bitrate index
}


/*-------------------------------------------------------------*/
int
XingHeader ( int samprate, int h_mode, int cr_bit, int original_bit,
             int head_flags, int frames, int bs_bytes,
             int vbr_scale,
             unsigned char *toc, unsigned char *buf, unsigned char *buf20,
             unsigned char *buf20B, int nBitRateIndex )
{
    int i, k;
    int h_id;
    int sr_index;
    int br_index;
    int side_bytes;
    int bytes_required;
    int frame_bytes;
    int tmp;
    unsigned char *buf0;

    //------------ clear toc table
    for ( i = 0; i < N; i++ )
        table[i][0] = table[i][1] = 0;
    toc_ptr = 0;
    toc_n = 1;

    h_mode = h_mode & 3;
    cr_bit = cr_bit & 1;
    original_bit = original_bit & 1;
    head_flags = head_flags & 127;       // max current flags
    buf0 = buf;

    for ( sr_index = 0; sr_index < 6; sr_index++ )
    {
        if ( samprate == sr_table[sr_index] )
            break;
    }
    if ( sr_index >= 6 )
        return 0;       // fail

    h_id = 0;   // mpeg2

    if ( sr_index >= 3 )
    {
        h_id = 1;       // mpeg1
        sr_index -= 3;
    }

    if ( h_id )
    {   // mpeg1
        side_bytes = 32;
        if ( h_mode == 3 )
            side_bytes = 17;
    }
    else
    {
        // mpeg2
        side_bytes = 17;
        if ( h_mode == 3 )
            side_bytes = 9;
    }

    // if CBR turn off TOC for low bitrates (it may be too large)
    if ( vbr_scale == -1 && br_table[h_id][nBitRateIndex] < 64 )
    {
        head_flags &= ~TOC_FLAG;
    }

    // determine required br_index
    bytes_required = 4 + side_bytes + ( 4 + 4 );        // "xing" + flags

    if ( head_flags & FRAMES_FLAG )
        bytes_required += 4;
    if ( head_flags & BYTES_FLAG )
        bytes_required += 4;
    if ( head_flags & TOC_FLAG )
        bytes_required += 100;
    if ( head_flags & VBR_SCALE_FLAG )
        bytes_required += 4;

    if ( head_flags & VBR_RESERVEDA_FLAG )
        bytes_required += 20;
    if ( head_flags & VBR_RESERVEDB_FLAG )
        bytes_required += 20;

    if ( head_flags & INFOTAG_FLAG )
        bytes_required += 36;

    tmp = samprate;
    if ( h_id == 0 )
        tmp += tmp;

    // if TRUE VBR 
    if ( vbr_scale != -1)
    {
        for ( br_index = 1; br_index < 15; br_index++ )
        {
            frame_bytes = 144000 * br_table[h_id][br_index] / tmp;
            if ( frame_bytes >= bytes_required )
                break;
        }
        if ( br_index >= 15 )
            return 0;   //fail, won't fit
    }

    // if VBR CBR version
    else
    {
        if ( nBitRateIndex >= 15 )
            return 0;   //fail, won't fit

        // use the index already determined from head info - you only have one shot 
        frame_bytes = 144000 * br_table[h_id][nBitRateIndex] / tmp;
        if ( frame_bytes < bytes_required )
            return 0;   //fail, won't fit

        // this value is needed below
        br_index = nBitRateIndex;
    }

    buf[0] = 0xFF;
    buf[1] = 0xF3 | ( h_id << 3 );
    buf[2] = ( br_index << 4 ) | ( sr_index << 2 );
    buf[3] = ( h_mode << 6 ) | ( cr_bit << 3 ) | ( original_bit << 2 );
    buf += 4;

    for ( i = 0; i < side_bytes; i++ )
        buf[i] = 0;
    buf += side_bytes;

    if ( vbr_scale != -1 )
    {
        // VBR
        InsertString(buf, tag_vbr, 4);
    }
    else
    {
        // CBR
        InsertString(buf, tag_cbr, 4);
    }
    buf += 4;

    InsertI4 ( buf, head_flags );
    buf += 4;

    if ( head_flags & FRAMES_FLAG )
    {
        InsertI4 ( buf, frames );
        buf += 4;
    }

    if ( head_flags & BYTES_FLAG )
    {
        InsertI4 ( buf, bs_bytes );
        buf += 4;
    }

    if ( head_flags & TOC_FLAG )
    {
        if ( toc != NULL )
        {
            for ( i = 0; i < 100; i++ )
                buf[i] = toc[i];
        }
        else
        {
            for ( i = 0; i < 100; i++ )
                buf[i] = 0;     // reserve space
        }
        buf += 100;
    }

    if ( head_flags & VBR_SCALE_FLAG )
    {
        InsertI4 ( buf, vbr_scale );
        buf += 4;
    }

    if ( head_flags & VBR_RESERVEDA_FLAG )
    {
        if ( buf20 != NULL )
        {
            for ( i = 0; i < 20; i++ )
                buf[i] = buf20[i];
        }
        else
        {
            for ( i = 0; i < 20; i++ )
                buf[i] = 0;     // reserve space
        }
        buf += 20;
    }

    if ( head_flags & VBR_RESERVEDB_FLAG )
    {
        if ( buf20 != NULL )
        {
            for ( i = 0; i < 20; i++ )
                buf[i] = buf20B[i];
        }
        else
        {
            for ( i = 0; i < 20; i++ )
                buf[i] = 0;     // reserve space
        }
        buf += 20;
    }

    // fill remainder of frame
    k = frame_bytes - ( buf - buf0 );

    for ( i = 0; i < k; i++ )
        buf[i] = 0;

    return frame_bytes;
}



// old version, for compatiblity
int
XingHeaderUpdate ( int frames, int bs_bytes,
                   int vbr_scale,
                   unsigned char *toc, unsigned char *buf,
                   unsigned char *buf20, unsigned char *buf20B)
{
    // call new version with LAME info tag support, with default values for new parameters
    return XingHeaderUpdateInfo( frames, bs_bytes, vbr_scale, toc, buf, buf20, buf20B, 0, 0, 0, 0 );
}

/*-------------------------------------------------------------*/
int
XingHeaderUpdateInfo ( int frames, int bs_bytes,
                   int vbr_scale,
                   unsigned char *toc, unsigned char *buf,
                   unsigned char *buf20, unsigned char *buf20B ,
                   unsigned long samples_audio,
                   unsigned int bytes_mp3, unsigned int lowpass,
                   unsigned int in_samplerate)
                   
{
    int i, head_flags;
    int h_id, h_mode;

// update info in header
// using logic similar to logic that might be used by a decoder

    h_id = ( buf[1] >> 3 ) & 1;
    h_mode = ( buf[3] >> 6 ) & 3;

    if ( h_id )
    {   // mpeg1
        if ( h_mode != 3 )
            buf += ( 32 + 4 );
        else
            buf += ( 17 + 4 );
    }
    else
    {   // mpeg2
        if ( h_mode != 3 )
            buf += ( 17 + 4 );
        else
            buf += ( 9 + 4 );
    }

    if ( vbr_scale != -1 )
    {
        // VBR
        if(!BufStringEqual(buf, tag_vbr, 4))
        {
            return 0;
        }
    }
    else
    {
        // CBR
        if(!BufStringEqual(buf, tag_cbr, 4))
        {
            return 0;
        }
    }
    buf += 4;

    head_flags = ExtractI4 ( buf );
    buf += 4;   // get flags

    if ( head_flags & FRAMES_FLAG )
    {
        InsertI4 ( buf, frames );
        buf += 4;
    }
    if ( head_flags & BYTES_FLAG )
    {
        InsertI4 ( buf, bs_bytes );
        buf += 4;
    }

    if ( head_flags & TOC_FLAG )
    {
        if ( toc != NULL )
        {       // user supplied toc
            for ( i = 0; i < 100; i++ )
                buf[i] = toc[i];
        }
        else
        {
            BuildTOC ( frames, bs_bytes, buf );
        }
        buf += 100;
    }
    if ( head_flags & VBR_SCALE_FLAG )
    {
        InsertI4 ( buf, vbr_scale );
        buf += 4;
    }

    if ( head_flags & VBR_RESERVEDA_FLAG )
    {
        if ( buf20 != NULL )
        {       // user supplied buf20
            for ( i = 0; i < 20; i++ )
                buf[i] = buf20[i];
        }
        else
        {
            for ( i = 0; i < 20; i++ )
                buf[i] = 0;     // reserve space
        }
        buf += 20;
    }

    if ( head_flags & VBR_RESERVEDB_FLAG )
    {
        if ( buf20 != NULL )
        {       // user supplied buf20
            for ( i = 0; i < 20; i++ )
                buf[i] = buf20B[i];
        }
        else
        {
            for ( i = 0; i < 20; i++ )
                buf[i] = 0;     // reserve space
        }
        buf += 20;
    }

    if (head_flags & INFOTAG_FLAG && samples_audio != 0)
    {
        // Encoder short VersionString
        InsertString(buf, tag_shortversion, 9);
        buf += 9;
        
        // Info Tag revision + VBR method
        // 0x0 (rev.0) | (0x0 ('unknown VBR') or 0x1 ('constant bitrate')
        buf[0] = (0x0 << 4) | (vbr_scale != -1 ? 0x0 : 0x1);
        buf++;

        // Lowpass filter value
        buf[0] = (lowpass / 100) & 0xFF;
        buf++;

        // ReplayGain
        InsertI4 ( buf, 0 );
        buf += 4;
        InsertI4 ( buf, 0 );
        buf += 4;

        // Encoding flags + ATH Type
        buf[0] = 0; // (no --nspsytune, no --nssafejoint, ATH type 0)
        buf++;

        // specified bitrate if ABR, minimal bitrate otherwise
        buf[0] = 0; // (unknown)
        buf++;
        
        // encoder delays
        long samples_mp3 = frames * (h_id == 1 ? 1152 : 576);
        long pad_total = samples_mp3 - samples_audio;
        int pad_start = 1680;
        int pad_end = pad_total - pad_start;
        buf[0] = (pad_start >> 4) & 0xFF;
        buf[1] = ((pad_start << 4) & 0xF0 ) | ((pad_end >> 8) & 0x0F);
        buf[2] = (pad_end & 0xFF);
        buf += 3;

        // Misc
        buf[0] = 0x1C;
        if(in_samplerate == 44100)      buf[0] |= 0x40;
        else if(in_samplerate == 48000) buf[0] |= 0x80;
        else if(in_samplerate > 48000)  buf[0] |= 0xC0;
        buf++;

        // MP3 Gain
        buf[0] = 0; // 0 dB change
        buf++;

        // Preset and surround info
        buf[0] = 0;
        buf[1] = 0;
        buf += 2;

        // MusicLength
        InsertI4 ( buf, bytes_mp3 ); 
        buf += 4;

        // MusicCRC, TODO: compute CRC
        buf[0] = 0;
        buf[1] = 0;
        buf += 2;

        // Info Tag CRC, TODO: compute CRC
        buf[0] = 0;
        buf[1] = 0;
        buf += 2;
    }

    return 1;   // success
}

/*-------------------------------------------------------------*/
int
XingHeaderTOC ( int frames, int bs_bytes )
{
    int i, k;

// build toc

    table[toc_ptr][0] = frames;
    table[toc_ptr][1] = bs_bytes;
    toc_ptr++;
    if ( toc_ptr < N )
        return toc_n;

// decimate table
    for ( k = 1, i = 0; i < ( N / 2 ); i++, k += 2 )
    {
        table[i][0] = table[k][0];
        table[i][1] = table[k][1];
    }
    toc_ptr = ( N / 2 );
    toc_n += toc_n;

    return toc_n;
}

/*-------------------------------------------------------------*/
