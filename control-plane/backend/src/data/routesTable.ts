import { SipRouteSnapshot } from '../types/sipRoutes';

// Static stand-in for the future DB-backed routing table.
export const routesTable: SipRouteSnapshot = {
  table_id: 'default',
  version: 1,
  routes: {
    '1': {
      uri: 'sip:callee@sbc.local',
      sip_address: '127.0.0.1',
      port: 5080,
      codec: null,
    },
  },
};
