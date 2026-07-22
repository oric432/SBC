import { StatusCodes } from 'http-status-codes';
import { Request, Response, NextFunction } from 'express';
import { DrizzleQueryError } from 'drizzle-orm';
import { logger } from '../utils/logger';
import { sendError } from '../utils/apiResponse';
import { CustomError } from '../utils/interfaces';

const DB_UNREACHABLE_CODES = new Set(['ECONNREFUSED', 'ENOTFOUND', 'ETIMEDOUT']);

const isDbUnreachable = (err: CustomError): boolean => {
  if (err instanceof DrizzleQueryError) {
    const cause = err.cause as NodeJS.ErrnoException | undefined;
    return !!cause && DB_UNREACHABLE_CODES.has(cause.code || '');
  }
  const code = (err as NodeJS.ErrnoException).code;
  return !!code && DB_UNREACHABLE_CODES.has(code);
};

const errorHandlerMiddleware = (
  err: CustomError,
  _req: Request,
  res: Response,
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  _next: NextFunction,
) => {
  logger.error('Request failed', err);

  if (isDbUnreachable(err)) {
    return sendError(
      res,
      'Database unavailable, try again later',
      StatusCodes.SERVICE_UNAVAILABLE,
    );
  }

  const statusCode = err.code || StatusCodes.INTERNAL_SERVER_ERROR;
  const message = err.message || 'Something went wrong, try again later';

  return sendError(res, message, statusCode);
};

export default errorHandlerMiddleware;
