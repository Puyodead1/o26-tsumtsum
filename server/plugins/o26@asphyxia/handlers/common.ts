/*
 * Shop-level calls. Response layouts from tmac.dll's xrpc method table (0x1802A7E40): each entry is
 * {method, ?, request builder, response parser}; the parsers read fields with property_node_refer
 * (XCnbrep70000af) and fail the call when a required field is missing.
 */
import { big } from './util';

const now = () => big(Date.now());

/*
 * common.get (parser sub_1801941C0): phase_bit + expire required; message / event_data lists optional.
 * A message is {tag str, ver s16, message1 str, message2 str, from_time u64, till_time u64} (all required); the game
 * reads "KEY=a,b,c;KEY2=..." settings from message1/2 of messages with a given tag and ver 0, valid while from <= now
 * <= till or both 0 (sub_18016F110). Tag MASCOT (sub_180014180): RELEASE = the figurine series (Japanese numbering,
 * the game maps them to the cabinet's region) whose figurines may be scanned, ZERO_RELEASE = tsum ids allowed from
 * series 0 (Party Mickey = 97). Without it only series 1 works and everything else is "cannot be used with the
 * current game version".
 */
const MASCOT_SERIES = Array.from({ length: 19 }, (_, i) => i + 1);

export const commonGet: EPR = async (info, data, send) => {
  await send.object({
    phase_bit: K.ITEM('s32', 0),
    expire: K.ITEM('s32', 3600),
    message: [
      {
        tag: K.ITEM('str', 'MASCOT'),
        ver: K.ITEM('s16', 0),
        message1: K.ITEM('str', `RELEASE=${MASCOT_SERIES.join(',')};ZERO_RELEASE=97`),
        message2: K.ITEM('str', ''),
        from_time: K.ITEM('u64', big(0)),
        till_time: K.ITEM('u64', big(0)),
      },
    ],
  });
};

/* common.campaign (request val_type/val, parser sub_180194680): cstatus required; name when cstatus is 0/5/16/17. */
export const commonCampaign: EPR = async (info, data, send) => {
  await send.object({
    cstatus: K.ITEM('s32', 1),
    name: K.ITEM('str', ''),
  });
};

/* ranking.get (parser sub_180193070): expiration_date required; rankings and histograms optional. */
export const rankingGet: EPR = async (info, data, send) => {
  const profiles = await DB.Find<any>(null, { collection: 'profile' });
  const modes: { [mode: number]: any[] } = {};
  for (const p of profiles) {
    for (const g of p.total_game_data || []) {
      if (!g.max_score) continue;
      (modes[g.mode_id] = modes[g.mode_id] || []).push({ p, g });
    }
  }
  const mode_ranking = Object.keys(modes).map(m => {
    const rows = modes[m].sort((a, b) => b.g.max_score - a.g.max_score).slice(0, 500);
    return {
      mode_id: K.ITEM('u16', Number(m)),
      rank_data: rows.map((r, i) => ({
        rank: K.ITEM('s32', i + 1),
        tm_id: K.ITEM('u32', r.p.tm_id),
        name: K.ITEM('str', r.p.name),
        skin_id: K.ITEM('s32', r.g.skin_id || 0),
        score: K.ITEM('s32', r.g.max_score),
        score_update_party: K.ITEM('str', r.g.score_update_party || ''),
        score_update_date: K.ITEM('u64', big(r.g.update_date || Date.now())),
        pref_id: K.ITEM('s32', 13),
        opname: K.ITEM('str', 'Asphyxia'),
        level: K.ITEM('s16', r.p.level || 1),
        grade: K.ITEM('u32', 0),
      })),
    };
  });
  await send.object({
    expiration_date: K.ITEM('u64', now() + big(3600 * 1000)),
    total_ranking: { mode_ranking },
    shop_ranking: { mode_ranking },
  });
};

/* Fire-and-forget calls: no parser, any reply is fine. */
export const ok: EPR = async (info, data, send) => {
  await send.success();
};

/*
 * Mascot figures. A figure is identified by funique_id (the tag UID as 16 hex digits) and belongs to at most one
 * player once registered ("attach"). Stored plugin-wide: { collection: 'figure', funique_id, tm_id, ... }.
 * figure.check status: 0 = registered to this player, 1 = unregistered (the game offers to register it, then
 * sends figure.attach), FIGURE_OTHER = registered to someone else (name = owner; this code is a guess, not yet
 * seen in game).
 */
const FIGURE_OTHER = 2;

interface Figure {
  collection: 'figure';
  funique_id: string;
  tm_id: number;
  skin_id: number;
  chara_id: number;
  chara_type: number;
  kind_id: number;
  face_id: number;
  level_max: number;
  skill_level: number;
  skill_this_exp_per: number;
  skill_total_exp: number;
}

const ownerName = async (tm_id: number) => {
  const p = await DB.FindOne<any>(null, { collection: 'profile', tm_id });
  return p ? p.name : '';
};

/* figure.check (request sub_180194830, parser sub_180194950): one figure_data {status, funique_id, name} each. */
export const figureCheck: EPR = async (info, data, send) => {
  const tm_id = $(data).number('tm_id');
  const figure_data = [];
  for (const f of $(data).elements('figure_data')) {
    const funique_id = f.str('funique_id', '');
    const fig = await DB.FindOne<Figure>({ collection: 'figure', funique_id });
    const status = !fig ? 1 : fig.tm_id === tm_id ? 0 : FIGURE_OTHER;
    figure_data.push({
      status: K.ITEM('s32', status),
      funique_id: K.ITEM('str', funique_id),
      name: K.ITEM('str', status === FIGURE_OTHER ? await ownerName(fig.tm_id) : ''),
    });
  }
  await send.object({ figure_data });
};

/* figure.attach (request sub_180194AD0, parser sub_180194D70): register figures; ones owned by others go back as
   ng_figure_data {status, funique_id, name}. */
export const figureAttach: EPR = async (info, data, send) => {
  const tm_id = $(data).number('tm_id');
  const ng_figure_data = [];
  for (const f of $(data).elements('figure_data')) {
    const funique_id = f.str('funique_id', '');
    const fig = await DB.FindOne<Figure>({ collection: 'figure', funique_id });
    if (fig && fig.tm_id !== tm_id) {
      ng_figure_data.push({
        status: K.ITEM('s32', FIGURE_OTHER),
        funique_id: K.ITEM('str', funique_id),
        name: K.ITEM('str', await ownerName(fig.tm_id)),
      });
      continue;
    }
    const doc: Figure = {
      collection: 'figure',
      funique_id,
      tm_id,
      skin_id: f.number('skin_id'),
      chara_id: f.number('chara_id'),
      chara_type: f.number('chara_type'),
      kind_id: f.number('kind_id'),
      face_id: f.number('face_id'),
      level_max: f.number('level_max'),
      skill_level: f.number('skill_level'),
      skill_this_exp_per: f.number('skill_this_exp_per'),
      skill_total_exp: f.number('skill_total_exp'),
    };
    await DB.Upsert<Figure>({ collection: 'figure', funique_id }, doc);
  }
  await send.object({ ng_figure_data });
};

/* figure.unattach (request sub_180194EC0, no parser): figure_data {funique_id} each. */
export const figureUnattach: EPR = async (info, data, send) => {
  for (const f of $(data).elements('figure_data')) {
    await DB.Remove<Figure>({ collection: 'figure', funique_id: f.str('funique_id', '') });
  }
  await send.success();
};

/* The player's registered figures, for pc.get. */
export const figuresOf = async (tm_id: number) =>
  (await DB.Find<Figure>({ collection: 'figure', tm_id })).map(f => f.funique_id);
