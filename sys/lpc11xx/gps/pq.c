/*
  position queue
  
*/

#include <stddef.h>
#include <string.h>
#include "datecalc.h"
#include "pq.h"


void pq_Init(pq_t *pq)
{
  memset(pq, 0, sizeof(pq_t));
  crb_Init(&(pq->crb));
}

void pq_AddStr(pq_t *pq, const char *str)
{
  crb_AddStr(&(pq->crb), str);  
}

void pq_DeleteFirst(pq_t *pq)
{
  uint8_t i;
  if ( pq->cnt > 0 )
  {
    for( i = 1; i < pq->cnt; i++ )
    {
      pq->queue[i-1] = pq->queue[i];
    }
    pq->cnt--;
  }
}

/* add values from the interface to the queue record */
void pq_AddInterfaceValuesToQueue(pq_t *pq)
{
  if ( pq->cnt >= PQ_LEN )
    pq_DeleteFirst(pq);
  pq->queue[pq->cnt].pos = pq->interface.pos;
  pq->cnt++;
}

pq_entry_t *pq_GetLatestEntry(pq_t *pq)
{
  if ( pq->cnt == 0 )
    return NULL;
  return &(pq->queue[pq->cnt-1]);
}


/*===========================================*/


void pq_ResetParser(pq_t *pq)
{
  crb_GetInit(&(pq->crb));  
}

int16_t pq_GetCurr(pq_t *pq)
{
  return crb_GetCurr(&(pq->crb));
}

int16_t pq_GetNext(pq_t *pq)
{
  return crb_GetNext(&(pq->crb));
}

uint8_t pq_SkipSpace(pq_t *pq)
{
  int16_t c;
  c = pq_GetCurr(pq);
  if ( c < 0 )
    return 0;
  for(;;)
  {
    if ( c < 0 || c > 32 )
      break;
    c = pq_GetNext(pq);
  }
  return 1;
}


uint8_t pq_GetNum(pq_t *pq, uint32_t *num, uint8_t *digit_cnt)
{
  int16_t c;
  if ( pq_SkipSpace(pq) == 0 )
    return 0;
  if ( digit_cnt != NULL )
    *digit_cnt = 0;
  *num = 0L;
  c = pq_GetCurr(pq);
  if ( c < 0 )
    return 0;
  for(;;)
  {
    if ( c < '0' || c > '9' )
      break;
    *num *= 10L;
    *num += c - '0';
    if ( digit_cnt != NULL )
      *digit_cnt += 1;
    c = pq_GetNext(pq);
  }
  return 1;
}

uint8_t pq_GetFloat(pq_t *pq, gps_float_t *f)
{
  uint8_t digit_cnt;
  uint32_t num;
  if ( pq_GetNum(pq, &num, NULL) == 0 )
    return 0;
  *f = (gps_float_t)num;
  if ( pq_GetCurr(pq) == '.' )
  {
    pq_GetNext(pq);
    if ( pq_GetNum(pq, &num, &digit_cnt) == 0 )
      return 0;
    
    {
      gps_float_t g;
      g = (gps_float_t)num;
      while( digit_cnt > 0 )
      {
	g /= (gps_float_t)10;
	digit_cnt--;
      }
      *f += g;
    }
  }
  return 1;
}

/* read a small string, max 8 bytes, including '\0' */ 
#define PQ_STR_MAX 8
const char *pq_GetStr(pq_t *pq)
{
  static char buf[PQ_STR_MAX];
  uint8_t pos;
  int16_t c;
  
  if ( pq_SkipSpace(pq) == 0 )
    return NULL;
  c = pq_GetCurr(pq);
  pos = 0;
  while( (c >=64 || c == '$') && pos < PQ_STR_MAX-1 )
  {
    buf[pos] = c;
    c = pq_GetNext(pq);
    pos++;
  }
  buf[pos] = '\0';
  return buf;
}

uint8_t pq_CheckChar(pq_t *pq, int16_t c)
{
  if ( pq_SkipSpace(pq) == 0 )
    return 0;
  if ( pq_GetCurr(pq) != c )
    return 0;
  pq_GetNext(pq);
  return 1;
}

uint8_t pq_CheckComma(pq_t *pq)
{
  return pq_CheckChar(pq, ',');
}

/* *result is 0 for c1 and 1 for c2, undefined if none of the two is matched */
/* return value is 0 if none has matched */
uint8_t pq_CheckTwoChars(pq_t *pq, uint8_t *result, int16_t c1, int16_t c2)
{
  if ( pq_CheckChar(pq, c1) != 0 )
  {
    *result = 0;
    return 1;
  }
  if ( pq_CheckChar(pq, c2) != 0 )
  {
    *result = 1;
    return 1;
  }
  return 0;
}

/*
  Assumes, that $GPRMC is alread paresed

$GPRMC,220516,A,5133.82,N,00042.24,W,173.8,231.8,130694,004.2,W*70
              1    2    3    4    5     6    7    8      9     10  11 12


      1   220516     Time Stamp
      2   A          validity - A-ok, V-invalid
      3   5133.82    current Latitude
      4   N          North/South
      5   00042.24   current Longitude
      6   W          East/West
      7   173.8      Speed in knots
      8   231.8      True course
      9   130694     Date Stamp
      10  004.2      Variation
      11  W          East/West
      12  *70        checksum

Latitude
  S --> negative
  
Longitude
  W --> negative

*/
uint8_t pq_ParseGPRMC(pq_t *pq)
{
  gps_float_t time;
  uint32_t date;
  uint8_t is_valid;
  uint8_t is_south;
  uint8_t is_west;
  if ( pq_CheckComma(pq) == 0 ) return 0;
  if ( pq_GetFloat(pq, &time) == 0 ) return 0;
  if ( pq_CheckComma(pq) == 0 ) return 0;
  if ( pq_CheckTwoChars(pq, &is_valid, 'V', 'A') == 0 ) return 0;
  if ( pq_CheckComma(pq) == 0 ) return 0;
  if ( pq_GetFloat(pq, &(pq->interface.pos.latitude)) == 0 ) return 0;
  if ( pq_CheckComma(pq) == 0 ) return 0;
  if ( pq_CheckTwoChars(pq, &is_south, 'N', 'S') == 0 ) return 0;
  if ( is_south != 0 ) pq->interface.pos.latitude = -pq->interface.pos.latitude;
  if ( pq_CheckComma(pq) == 0 ) return 0;
  if ( pq_GetFloat(pq, &(pq->interface.pos.longitude)) == 0 ) return 0;
  if ( pq_CheckComma(pq) == 0 ) return 0;
  if ( pq_CheckTwoChars(pq, &is_west, 'E', 'W') == 0 ) return 0;
  if ( is_west != 0 ) pq->interface.pos.longitude = -pq->interface.pos.longitude;
  if ( pq_CheckComma(pq) == 0 ) return 0;
  if ( pq_GetFloat(pq, &(pq->interface.speed_in_knots)) == 0 ) return 0;
  if ( pq_CheckComma(pq) == 0 ) return 0;
  if ( pq_GetFloat(pq, &(pq->interface.true_course)) == 0 ) return 0;
  if ( pq_CheckComma(pq) == 0 ) return 0;
  if ( pq_GetNum(pq, &date, NULL) == 0 ) return 0;  
  if ( pq_CheckComma(pq) == 0 ) return 0;
  if ( pq_GetFloat(pq, &(pq->interface.magnetic_variation)) == 0 ) return 0;
  if ( pq_CheckComma(pq) == 0 ) return 0;
  if ( pq_CheckTwoChars(pq, &is_west, 'E', 'W') == 0 ) return 0;
  if ( is_west != 0 ) pq->interface.pos.longitude = -pq->interface.pos.longitude;
  if ( is_valid != 0 )
  {
    pq_AddInterfaceValuesToQueue(pq);
    pq->valid_gprmc++;
  }
  else
  {
    pq->invalid_gprmc++;
  }
  return 1;
}


/* return 0 on parsing error */
uint8_t pq_ParseSentence(pq_t *pq)
{
  uint8_t result = 0;
  const char *s;
  if ( crb_IsSentenceAvailable(&(pq->crb)) == 0 )
    return 1;
  s = pq_GetStr(pq);
  if ( s != NULL )
  {
    if (  strcmp(s, "$GPRMC") == 0 )
    {
      result = pq_ParseGPRMC(pq);
    }
  } 
  crb_DeleteSentence(&(pq->crb));
  pq->processed_sentences++;
  return result;
}


