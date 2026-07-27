export interface CustomError extends Error {
  code?: number;
  statusCode?: number;
  meta?: {target?: string;};
}
