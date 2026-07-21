import { Response } from 'express';
import { StatusCodes } from 'http-status-codes';

export interface ApiSuccess<T> {
  success: true;
  data: T;
}

export interface ApiError {
  success: false;
  error: {
    message: string;
    details?: unknown;
  };
}

export type ApiResponse<T> = ApiSuccess<T> | ApiError;

export const sendSuccess = <T>(
  res: Response,
  data: T,
  statusCode: number = StatusCodes.OK,
): void => {
  const body: ApiSuccess<T> = { success: true, data };
  res.status(statusCode).json(body);
};

export const sendError = (
  res: Response,
  message: string,
  statusCode: number = StatusCodes.INTERNAL_SERVER_ERROR,
  details?: unknown,
): void => {
  const body: ApiError = { success: false, error: { message, details } };
  res.status(statusCode).json(body);
};
