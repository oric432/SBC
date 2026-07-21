import {NextFunction, Request, Response} from 'express';
import {StatusCodes} from 'http-status-codes';
import {z, ZodSchema} from 'zod';
import {sendError} from '../utils/apiResponse';

const zodDetails = (error: z.ZodError) =>
  error.issues.map((err) => ({field: err.path.join('.'), message: err.message}));

export const validateBody = (schema: ZodSchema) => {
  return (req: Request, res: Response, next: NextFunction) => {
    try {
      req.body = schema.parse(req.body);
      next();
    } catch (error) {
      if (error instanceof z.ZodError) {
        return sendError(res, 'Validation failed', StatusCodes.BAD_REQUEST, zodDetails(error));
      }
      next(error);
    }
  };
};

export const validateParams = (schema: ZodSchema) => {
  return (req: Request, res: Response, next: NextFunction) => {
    try {
      req.params = schema.parse(req.params) as any;
      next();
    } catch (error) {
      if (error instanceof z.ZodError) {
        return sendError(res, 'Invalid parameters', StatusCodes.BAD_REQUEST, zodDetails(error));
      }
      next(error);
    }
  };
};

export const validateQuery = (schema: ZodSchema) => {
  return (req: Request, res: Response, next: NextFunction) => {
    try {
      req.query = schema.parse(req.query) as any;
      next();
    } catch (error) {
      if (error instanceof z.ZodError) {
        return sendError(
          res,
          'Invalid query parameters',
          StatusCodes.BAD_REQUEST,
          zodDetails(error),
        );
      }
      next(error);
    }
  };
};
