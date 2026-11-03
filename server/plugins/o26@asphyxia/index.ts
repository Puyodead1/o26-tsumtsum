/*
 * Disney Tsum Tsum (Konami O26) for Asphyxia CORE.
 * Protocol reversed from tmac.dll's jnet3 module and ess.dll (see the repo README, "Server").
 */
import { convcardnumber, eventlogWrite } from './handlers/system';
import { commonGet, commonCampaign, rankingGet, figureCheck, figureAttach, figureUnattach, ok } from './handlers/common';
import { pcReg, pcGet, pcSave } from './handlers/pc';

/* With the "log" option on, print every request this plugin handles (run CORE with --dev to see it). */
function route(method: string, handler: EPR) {
  R.Route(method, async (info, data, send) => {
    if (U.GetConfig('log')) console.log(`[O26] ${info.module}.${info.method}\n${U.toXML(data)}`);
    return handler(info, data, send);
  });
}

export function register() {
  R.GameCode('O26');
  R.Contributor('O26 preservation');
  R.Config('log', { name: 'Log requests', type: 'boolean', default: true });

  route('system.convcardnumber', convcardnumber);
  route('eventlog.write', eventlogWrite);

  route('common.get', commonGet);
  route('common.campaign', commonCampaign);
  route('common.anydata', ok);
  route('shop.save', ok);
  route('shop.update', ok);
  route('ranking.get', rankingGet);
  route('report.report', ok);

  route('figure.check', figureCheck);
  route('figure.attach', figureAttach);
  route('figure.unattach', figureUnattach);

  route('pc.reg', pcReg);
  route('pc.get', pcGet);
  route('pc.save', pcSave);
  route('pc.logout', ok);

  /* Log every call we don't handle yet, with its request body. */
  R.Unhandled(async (info, data, send) => {
    console.log(`[O26] unhandled ${info.module}.${info.method}\n${U.toXML(data)}`);
    await send.success();
  });
}
