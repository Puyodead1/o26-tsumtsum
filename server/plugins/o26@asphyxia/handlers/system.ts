/*
 * system.convcardnumber: ess.dll's receiver (sub_18001DB70) needs an s32 "result" and, when it is 0, clones
 * "/data"; ess_cardmng_conv_cardnumber_wait then reads the str "data/card_number" (up to 16 chars + NUL).
 * CORE has already replaced data/card_id with "refid|dataid", so the refid stands in for the printed number.
 */
import { big } from './util';

export const convcardnumber: EPR = async (info, data, send) => {
  const refid = $(data).str('data.card_id', '').split('|')[0];
  await send.object({
    result: K.ITEM('s32', 0),
    data: { card_number: K.ITEM('str', refid.substring(0, 16)) },
  });
};

/* eventlog.write: the usual Konami reply; without it the game resends the same event forever. */
export const eventlogWrite: EPR = async (info, data, send) => {
  await send.object({
    gamesession: K.ITEM('s64', big(1)),
    logsendflg: K.ITEM('s32', 0),
    logerrlevel: K.ITEM('s32', 0),
    evtidnosendflg: K.ITEM('s32', 0),
  });
};
