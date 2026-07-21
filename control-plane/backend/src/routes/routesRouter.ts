import { Router } from 'express';
import { getRoutes } from '../controllers/routesController';

const router = Router();

router.get('/', getRoutes);

export default router;
