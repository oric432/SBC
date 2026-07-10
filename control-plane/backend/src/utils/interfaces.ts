export interface CustomError extends Error {
  code?: number;
  meta?: {target?: string;};
}
