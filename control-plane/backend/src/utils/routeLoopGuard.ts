import { env } from '../config/env';
import { BadRequestError } from '../errors';

// A route whose target is the SBC's own SIP address/port would make the SBC
// forward a call back to itself, looping forever. Compare host case-
// insensitively since SIP hostnames/IPs aren't case sensitive.
export const assertNotRoutingLoop = (sipAddress: string, port: number) => {
  if (sipAddress.trim().toLowerCase() === env.sbcSipAddress.toLowerCase() && port === env.sbcSipPort) {
    throw new BadRequestError(
      `Route target ${sipAddress}:${port} is this SBC's own SIP address and would create a routing loop`,
    );
  }
};
