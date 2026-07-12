import { Request, Response } from 'express';
import { routesTable } from '../data/routesTable';

export const getRoutes = (_req: Request, res: Response) => {
  res.status(200).json(routesTable);
};
