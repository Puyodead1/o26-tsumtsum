/*
 * Player calls (module "pc"). Request builders / response parsers in tmac.dll:
 *   pc.reg    sub_180194F80 -> sub_180195110 (tm_id u32)
 *   pc.get    sub_180195170 -> sub_1801952D0 (whole profile, see sendProfile)
 *   pc.save   sub_180197240 -> sub_1801988C0 (period_id, grade_info_type, grade_certificate_url)
 *   pc.logout (no body either way)
 */
import { big } from './util';
import { figuresOf } from './common';

interface Mode {
  mode_id: number;
  max_score: number;
  last_score: number;
  play_num: number;
  skin_id: number;
  score_update_party: string;
  update_date?: number;
}

interface Detail {
  mode_id: number;
  score_sum: number;
  del_sum: number;
  fever_sum: number;
  skill_sum: number;
  del_sum_1play: number;
  fever_sum_1play: number;
  skill_sum_1play: number;
  chain_sum_1play: number;
  combo_sum_1play: number;
  coin_sum_1play: number;
}

interface Chara {
  chara_id: number;
  level: number;
  level_max: number;
  this_exp_per: number;
  total_exp: number;
  use_num: number;
  reg_num: number;
  skill_level: number;
  skill_this_exp_per: number;
  skill_total_exp: number;
}

interface Profile {
  collection: 'profile';
  tm_id: number;
  name: string;
  play_num: number;
  coin_num: number;
  heart_num: number;
  level: number;
  this_exp_per: number;
  total_exp: number;
  period_id: number;
  setting: { wallpaper: number; flag_1: number; flag_2: number; flag_3: number; flag_4: number };
  haveitem_data: { item_id: number; item_no: number }[];
  detail_data: Detail[];
  total_game_data: Mode[];
  period_game_data: Mode[];
  battle: { win_num: number; lose_num: number; draw_num: number; last_result: number };
  party_data: { skin_id: number; kind: number }[];
  chara_data: Chara[];
  figure_data: string[];
  skin_data: number[];
  item_data: { item_id: number; num: number }[];
  present: { stamp_num: number; ticket_num: number; applied_ticket_num: number; elected_num: number };
  bingo_pc: { bingo_id: number; play_num: number; clear_num: number; line_num: number; complete: boolean; info_sw: boolean; mission: { hole_no: number; last_val: number; clear: boolean }[] };
  last_answered_id: number;
  last_save?: any;
}

const num = (r: KDataReader, path: string) => {
  const v = r.content(path);
  return v === undefined || v === null ? 0 : Number(Array.isArray(v) ? v[0] : v);
};

async function nextTmId(): Promise<number> {
  const counter = await DB.FindOne<any>({ collection: 'tm_id' });
  const id = (counter ? counter.next : 100000001) as number;
  await DB.Upsert({ collection: 'tm_id' }, { collection: 'tm_id', next: id + 1 });
  return id;
}

function newProfile(tm_id: number, name: string): Profile {
  return {
    collection: 'profile',
    tm_id,
    name,
    play_num: 0,
    coin_num: 0,
    heart_num: 0,
    level: 1,
    this_exp_per: 0,
    total_exp: 0,
    period_id: 0,
    setting: { wallpaper: 0, flag_1: 0, flag_2: 0, flag_3: 0, flag_4: 0 },
    haveitem_data: [],
    detail_data: [],
    total_game_data: [],
    period_game_data: [],
    battle: { win_num: 0, lose_num: 0, draw_num: 0, last_result: 0 },
    party_data: [],
    chara_data: [],
    figure_data: [],
    skin_data: [],
    item_data: [],
    present: { stamp_num: 0, ticket_num: 0, applied_ticket_num: 0, elected_num: 0 },
    bingo_pc: { bingo_id: 0, play_num: 0, clear_num: 0, line_num: 0, complete: false, info_sw: false, mission: [] },
    last_answered_id: 0,
  };
}

const refidOf = (data: any) => $(data).str('refid', '');

export const pcReg: EPR = async (info, data, send) => {
  const refid = refidOf(data);
  let p = await DB.FindOne<Profile>(refid, { collection: 'profile' });
  if (!p) {
    const fresh = newProfile(await nextTmId(), $(data).str('name', 'PLAYER'));
    await DB.Upsert<Profile>(refid, { collection: 'profile' }, fresh);
    p = fresh as any;
  }
  await send.object({ tm_id: K.ITEM('u32', p.tm_id) });
};

const modeItems = (list: Mode[]) =>
  list.map(m => ({
    mode_id: K.ITEM('s16', m.mode_id),
    max_score: K.ITEM('s32', m.max_score),
    skin_id: K.ITEM('s32', m.skin_id),
    score_update_party: K.ITEM('str', m.score_update_party || ''),
    last_score: K.ITEM('s32', m.last_score),
    play_num: K.ITEM('s32', m.play_num),
  }));

/* The bingo card itself: 9 missions and 9 rewards are read unconditionally. No event: all zero. */
const bingoData = () => ({
  bingo_id: K.ITEM('s32', 0),
  name: K.ITEM('str', ''),
  title_id: K.ITEM('s32', 0),
  since: K.ITEM('u64', big(0)),
  till: K.ITEM('u64', big(0)),
  mission: [...Array(9)].map((_, i) => ({
    hole_no: K.ITEM('s32', i),
    hole_pos_x: K.ITEM('s32', i % 3),
    hole_pos_y: K.ITEM('s32', Math.floor(i / 3)),
    kind: K.ITEM('s32', 0),
    number: K.ITEM('s32', 0),
    /* read as "val#0".."val#2": AVS path syntax for the 1st..3rd <val> sibling */
    val: [K.ITEM('s32', 0), K.ITEM('s32', 0), K.ITEM('s32', 0)],
  })),
  reward: [...Array(9)].map(() => ({
    bingo_count: K.ITEM('s32', 0),
    kind: K.ITEM('s32', 0),
    num: K.ITEM('s32', 0),
  })),
});

function profileXml(p: Profile) {
  const b = p.bingo_pc;
  return {
    player_data: {
      tm_id: K.ITEM('u32', p.tm_id),
      name: K.ITEM('str', p.name),
      play_num: K.ITEM('s32', p.play_num),
      coin_num: K.ITEM('s32', p.coin_num),
      heart_num: K.ITEM('s64', big(p.heart_num)),
      level: K.ITEM('s16', p.level),
      this_exp_per: K.ITEM('s32', p.this_exp_per),
      total_exp: K.ITEM('s32', p.total_exp),
      period_id: K.ITEM('s32', p.period_id),
      setting_data: {
        wallpaper: K.ITEM('s32', p.setting.wallpaper),
        flag_1: K.ITEM('u32', p.setting.flag_1),
        flag_2: K.ITEM('u32', p.setting.flag_2),
        flag_3: K.ITEM('u32', p.setting.flag_3),
        flag_4: K.ITEM('u32', p.setting.flag_4),
      },
      haveitem_data: p.haveitem_data.map(h => ({ item_id: K.ITEM('s32', h.item_id), item_no: K.ITEM('s32', h.item_no) })),
      detail_data: p.detail_data.map(d => ({
        mode_id: K.ITEM('s32', d.mode_id),
        score_sum: K.ITEM('s64', big(d.score_sum)),
        del_sum: K.ITEM('s32', d.del_sum),
        fever_sum: K.ITEM('s32', d.fever_sum),
        skill_sum: K.ITEM('s32', d.skill_sum),
        del_sum_1play: K.ITEM('s32', d.del_sum_1play),
        fever_sum_1play: K.ITEM('s32', d.fever_sum_1play),
        skill_sum_1play: K.ITEM('s32', d.skill_sum_1play),
        chain_sum_1play: K.ITEM('s32', d.chain_sum_1play),
        combo_sum_1play: K.ITEM('s32', d.combo_sum_1play),
        coin_sum_1play: K.ITEM('s32', d.coin_sum_1play),
      })),
    },
    total_game_data: modeItems(p.total_game_data),
    period_game_data: modeItems(p.period_game_data),
    battle_data: {
      win_num: K.ITEM('s32', p.battle.win_num),
      lose_num: K.ITEM('s32', p.battle.lose_num),
      draw_num: K.ITEM('s32', p.battle.draw_num),
      last_result: K.ITEM('u8', p.battle.last_result),
    },
    party_data: p.party_data.map(x => ({ skin_id: K.ITEM('s32', x.skin_id), kind: K.ITEM('s32', x.kind) })),
    chara_data: p.chara_data.map(c => ({
      chara_id: K.ITEM('s16', c.chara_id),
      level: K.ITEM('s16', c.level),
      level_max: K.ITEM('s16', c.level_max),
      this_exp_per: K.ITEM('s32', c.this_exp_per),
      total_exp: K.ITEM('s32', c.total_exp),
      use_num: K.ITEM('s32', c.use_num),
      reg_num: K.ITEM('s32', c.reg_num),
      skill_level: K.ITEM('s32', c.skill_level),
      skill_this_exp_per: K.ITEM('s32', c.skill_this_exp_per),
      skill_total_exp: K.ITEM('s32', c.skill_total_exp),
    })),
    figure_data: p.figure_data.map(f => ({ funique_id: K.ITEM('str', f) })),
    skin_data: p.skin_data.map(s => ({ skin_id: K.ITEM('s32', s) })),
    item_data: p.item_data.map(i => ({ item_id: K.ITEM('s32', i.item_id), num: K.ITEM('s32', i.num) })),
    present_data: {
      stamp_num: K.ITEM('s32', p.present.stamp_num),
      ticket_num: K.ITEM('s32', p.present.ticket_num),
      applied_ticket_num: K.ITEM('s32', p.present.applied_ticket_num),
      elected_num: K.ITEM('s32', p.present.elected_num),
    },
    enquete_data: { last_answered_id: K.ITEM('s32', p.last_answered_id) },
    kac_data: { entry_flag: K.ITEM('bool', false) },
    bingo_data: bingoData(),
    bingo_pc_data: {
      bingo_id: K.ITEM('s32', b.bingo_id),
      play_num: K.ITEM('s32', b.play_num),
      clear_num: K.ITEM('s32', b.clear_num),
      line_num: K.ITEM('s32', b.line_num),
      complete: K.ITEM('bool', b.complete),
      info_sw: K.ITEM('bool', b.info_sw),
      mission: [...Array(9)].map((_, i) => {
        const m = b.mission[i] || { hole_no: i, last_val: 0, clear: false };
        return { hole_no: K.ITEM('s32', m.hole_no), last_val: K.ITEM('s32', m.last_val), clear: K.ITEM('bool', m.clear) };
      }),
    },
  };
}

export const pcGet: EPR = async (info, data, send) => {
  const p = await DB.FindOne<Profile>(refidOf(data), { collection: 'profile' });
  if (!p) {
    /* No profile: send nothing, which the parser rejects (player_data missing). */
    console.log(`[O26] pc.get: no profile for ${refidOf(data)}`);
    await send.success();
    return;
  }
  p.figure_data = await figuresOf(p.tm_id);
  await send.object(profileXml(p));
};

function upsertMode(list: Mode[], mode_id: number, score: number, skin_id: number, party: string) {
  let m = list.find(x => x.mode_id === mode_id);
  if (!m) list.push((m = { mode_id, max_score: 0, last_score: 0, play_num: 0, skin_id: 0, score_update_party: '' }));
  m.play_num++;
  m.last_score = score;
  if (score > m.max_score) {
    m.max_score = score;
    m.skin_id = skin_id;
    m.score_update_party = party;
    m.update_date = Date.now();
  }
}

export const pcSave: EPR = async (info, data, send) => {
  /* pc.save carries no refid; dataid is the refid CORE handed out through cardmng */
  const refid = refidOf(data) || $(data).str('dataid', '');
  const r = $(data);
  const p = await DB.FindOne<Profile>(refid, { collection: 'profile' });
  if (p && p.tm_id === num(r, 'tm_id')) {
    const party = r.str('party', '');
    const skin = num(r, 'leader_skin_id');
    p.play_num++;
    p.level = num(r, 'level') || p.level;
    p.this_exp_per = num(r, 'this_exp_per');
    p.total_exp = num(r, 'total_exp');
    p.coin_num = num(r, 'coin_num');
    p.heart_num = num(r, 'heart_num');
    p.period_id = num(r, 'period_id');
    p.setting = {
      wallpaper: num(r, 'setting_data.wallpaper'),
      flag_1: num(r, 'setting_data.flag_1'),
      flag_2: num(r, 'setting_data.flag_2'),
      flag_3: num(r, 'setting_data.flag_3'),
      flag_4: num(r, 'setting_data.flag_4'),
    };
    p.haveitem_data = r.elements('buy_item_data').map(e => ({ item_id: num(e, 'item_id'), item_no: num(e, 'item_no') }));
    for (const e of r.elements('item_data')) {
      const id = num(e, 'item_id');
      let it = p.item_data.find(x => x.item_id === id);
      if (!it) p.item_data.push((it = { item_id: id, num: 0 }));
      it.num = Math.max(0, it.num + num(e, 'get_num') - num(e, 'use_num'));
    }
    if (r.element('present_data')) {
      p.present = {
        stamp_num: num(r, 'present_data.stamp_num'),
        ticket_num: num(r, 'present_data.ticket_num'),
        applied_ticket_num: num(r, 'present_data.applied_ticket_num'),
        elected_num: num(r, 'present_data.elected_num'),
      };
    }
    for (const g of r.elements('game_data')) {
      const mode_id = num(g, 'mode_id');
      const score = num(g, 'score_sum');
      upsertMode(p.total_game_data, mode_id, score, skin, party);
      upsertMode(p.period_game_data, mode_id, score, skin, party);
      let d = p.detail_data.find(x => x.mode_id === mode_id);
      if (!d) {
        p.detail_data.push((d = { mode_id, score_sum: 0, del_sum: 0, fever_sum: 0, skill_sum: 0, del_sum_1play: 0, fever_sum_1play: 0, skill_sum_1play: 0, chain_sum_1play: 0, combo_sum_1play: 0, coin_sum_1play: 0 }));
      }
      d.score_sum += score;
      d.del_sum += num(g, 'del_sum');
      d.fever_sum += num(g, 'fever_sum');
      d.skill_sum += num(g, 'skill_sum');
      d.del_sum_1play = Math.max(d.del_sum_1play, num(g, 'del_sum'));
      d.fever_sum_1play = Math.max(d.fever_sum_1play, num(g, 'fever_sum'));
      d.skill_sum_1play = Math.max(d.skill_sum_1play, num(g, 'skill_sum'));
      d.chain_sum_1play = Math.max(d.chain_sum_1play, num(g, 'chain_sum'));
      d.combo_sum_1play = Math.max(d.combo_sum_1play, num(g, 'combo_sum'));
      d.coin_sum_1play = Math.max(d.coin_sum_1play, num(g, 'coin_sum'));
    }
    if (r.element('battle_data')) {
      p.battle.win_num = num(r, 'battle_data.win_num');
      p.battle.lose_num = num(r, 'battle_data.lose_num');
      p.battle.draw_num = num(r, 'battle_data.draw_num');
    }
    for (const e of r.elements('exp_data')) {
      const id = num(e, 'chara_id');
      let c = p.chara_data.find(x => x.chara_id === id);
      if (!c) p.chara_data.push((c = { chara_id: id, level: 1, level_max: 1, this_exp_per: 0, total_exp: 0, use_num: 0, reg_num: 0, skill_level: 1, skill_this_exp_per: 0, skill_total_exp: 0 }));
      c.level = num(e, 'level');
      c.level_max = num(e, 'level_max');
      c.this_exp_per = num(e, 'this_exp_per');
      c.total_exp = num(e, 'total_exp');
      c.skill_level = num(e, 'skill_level');
      c.skill_this_exp_per = num(e, 'skill_this_exp_per');
      c.skill_total_exp = num(e, 'skill_total_exp');
      c.use_num++;
    }
    /* party = 5 comma-separated skin ids, party_info = matching kinds (3 for the default tsums) */
    const kinds = r.str('party_info', '').split(',');
    const skins = party.split(',').map(Number).filter(s => s > 0);
    if (skins.length) p.party_data = skins.map((s, i) => ({ skin_id: s, kind: Number(kinds[i]) || 0 }));
    for (const s of [...skins, skin, num(r, 'leader_skin_id_2')]) if (s && !p.skin_data.includes(s)) p.skin_data.push(s);
    if (r.element('bingo_data')) {
      p.bingo_pc = {
        bingo_id: num(r, 'bingo_data.bingo_id'),
        play_num: num(r, 'bingo_data.play_num'),
        clear_num: num(r, 'bingo_data.clear_num'),
        line_num: num(r, 'bingo_data.line_num'),
        complete: r.bool('bingo_data.complete'),
        info_sw: r.bool('bingo_data.info_sw'),
        mission: r.elements('bingo_data.mission').map(m => ({ hole_no: num(m, 'hole_no'), last_val: num(m, 'last_val'), clear: m.bool('clear') })),
      };
    }
    p.last_save = U.toXML(data);
    /* $set only our fields: replacing the whole doc drops CORE's refid binding and CORE then deletes it */
    const { _id, __s, __refid, createdAt, updatedAt, ...fields } = p as any;
    await DB.Update<Profile>(refid, { collection: 'profile' }, { $set: fields } as any);
  } else {
    console.log(`[O26] pc.save: unknown tm_id ${num(r, 'tm_id')} (refid ${refid})`);
  }
  await send.object({
    period_id: K.ITEM('s32', p ? p.period_id : 0),
    grade_info_type: K.ITEM('s32', 0),
    grade_certificate_url: K.ITEM('str', ''),
  });
};
