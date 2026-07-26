import { StatusCodes } from 'http-status-codes';
import { Request, Response, NextFunction } from 'express';
import { DrizzleQueryError } from 'drizzle-orm';
import { logger } from '../utils/logger';
import { sendError } from '../utils/apiResponse';
import { CustomError } from '../utils/interfaces';

const NETWORK_UNREACHABLE_ERRNOS = new Set(['ECONNREFUSED', 'ENOTFOUND', 'ETIMEDOUT']);

// Postgres SQLSTATE codes are <2-char class><3-char specific case>, e.g.
// 23505 = class 23 (Integrity Constraint Violation) + unique_violation.
// Mapping by class covers every specific code in that family -- known or
// not yet seen -- without adding a branch per code.
// https://www.postgresql.org/docs/current/errcodes-appendix.html
const SQLSTATE_CLASS_RESPONSE: Record<string, { status: number; message: string }> = {
  '23': { status: StatusCodes.CONFLICT, message: 'A record with conflicting data already exists' },
  '22': { status: StatusCodes.BAD_REQUEST, message: 'Invalid data provided' },
};

const dbErrorCause = (err: CustomError): NodeJS.ErrnoException | undefined =>
  err instanceof DrizzleQueryError ? (err.cause as NodeJS.ErrnoException | undefined) : undefined;

const errorHandlerMiddleware = (
  err: CustomError,
  _req: Request,
  res: Response,
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  _next: NextFunction,
) => {
  const cause = dbErrorCause(err);

  // Expected/mapped cases: morgan already logs method/path/status for every
  // request, so the status code alone is enough context here -- no need to
  // duplicate it with an error-level log line.
  if (cause?.code && NETWORK_UNREACHABLE_ERRNOS.has(cause.code)) {
    return sendError(res, 'Database unavailable, try again later', StatusCodes.SERVICE_UNAVAILABLE);
  }

  const mapped = cause?.code && SQLSTATE_CLASS_RESPONSE[cause.code.slice(0, 2)];
  if (mapped) {
    return sendError(res, mapped.message, mapped.status);
  }

  // Unexpected/unmapped error -- an actual bug, not a known client-caused
  // case. Morgan's status line alone isn't enough here, so log the full
  // error for debugging.
  logger.error('Request failed', err);

  const status = err.statusCode || err.code || StatusCodes.INTERNAL_SERVER_ERROR;
  const message =
    status < StatusCodes.INTERNAL_SERVER_ERROR && err.message
      ? err.message
      : 'Something went wrong, try again later';

  return sendError(res, message, status);
};

export default errorHandlerMiddleware;
