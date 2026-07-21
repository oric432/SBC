import { StatusCodes } from 'http-status-codes';
import { Request, Response, NextFunction } from 'express';
import { CustomError } from '../utils/interfaces';

const errorHandlerMiddleware = (
  err: CustomError,
  _req: Request,
  res: Response,
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  _next: NextFunction,
) => {
  const customError = {
    code: err.code || StatusCodes.INTERNAL_SERVER_ERROR,
    msg: err.message || 'Something went wrong, try again later',
  };

  return res.status(customError.code).json({ msg: customError.msg });
};

export default errorHandlerMiddleware;
