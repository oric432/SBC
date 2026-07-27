import { Router } from 'express';
import { createRoute, deleteRoute, getRoutes, updateRoute } from '../controllers/routesController';
import { validateBody, validateParams } from '../middlewares/validation';
import { priorityParamSchema, routeBodySchema } from '../schemas/routesSchemas';

const router = Router();

router.get('/', getRoutes);
router.post('/', validateBody(routeBodySchema), createRoute);
router.put(
  '/:priority',
  validateParams(priorityParamSchema),
  validateBody(routeBodySchema),
  updateRoute,
);
router.delete('/:priority', validateParams(priorityParamSchema), deleteRoute);

export default router;
