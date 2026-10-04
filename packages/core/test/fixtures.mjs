// Shared test network: four friends' hubs + a steward, all grants wired, one mailbox.
import { createHub, grantAddress, createRecipientMailbox, foldRegistry, newIdentity, vouch, merge, categoryId } from "../src/index.mjs";

export const T0 = Date.UTC(2026, 9, 5, 9, 0, 0); // a Monday

export const CITIES = [
  { name: "Mule Prague", city: "Prague", country: "CZ" },
  { name: "Mule Berlin", city: "Berlin", country: "DE" },
  { name: "Mule Lisboa", city: "Lisboa", country: "PT" },
  { name: "Mule Porto", city: "Porto", country: "PT" },
  { name: "Mule Madrid", city: "Madrid", country: "ES" },
];

export function network({ accepts = ["*"], cities = CITIES } = {}) {
  const now = () => T0;
  const steward = newIdentity(undefined, now);
  const hubs = cities.map((c) => createHub({
    ...c,
    policy: { accepts, boxClasses: [1, 2, 3], maxWeightClass: 3, shipsTo: ["*"], batchDays: [1, 4], holdMaxDays: 7 },
    intake: { kind: "meetup", text: `${c.city} Logos Circle monthly meetup` },
  }, { now }));
  const log = [...hubs.map((h) => h.event), ...hubs.map((h) => vouch(steward, h.hub.address))];
  let state = foldRegistry(log, [steward.address]);
  // Every hub grants every other hub its (private) locker address.
  for (const to of hubs) for (const from of hubs) {
    if (to === from) continue;
    log.push(grantAddress(to.hub, state.hubs.get(from.hub.address), {
      carrier: "InPost", locker: `${to.hub.card.city.toUpperCase().slice(0, 3)}-L${hubs.indexOf(to)}`, name: `hub-${hubs.indexOf(to)}`, phone: "+000",
    }));
  }
  state = foldRegistry(log, [steward.address]);
  const exit = hubs[hubs.length - 1];
  const mb = createRecipientMailbox(state.hubs.get(exit.hub.address), { carrier: "Correos", locker: "MAD-CITYLOCKER-7", name: "R. Ecipient", phone: "+34..." });
  log.push(mb.event);
  state = foldRegistry(merge(log), [steward.address]);
  return { hubs: hubs.map((h) => h.hub), steward, log, state, mailbox: mb, now };
}

export const PARCEL = { category: categoryId("printed-object"), boxClass: 1, weightClass: 1, coreGrams: 180 };
