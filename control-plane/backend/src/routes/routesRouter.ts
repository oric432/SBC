import { Router } from 'express';
import { createRoute, deleteRoute, getRoutes, swapRoute, updateRoute } from '../controllers/routesController';
import { validateBody, validateParams } from '../middlewares/validation';
import { priorityParamSchema, routeBodySchema, swapRouteBodySchema } from '../schemas/routesSchemas';

const router = Router();

router.get('/', getRoutes);
router.post('/', validateBody(routeBodySchema), createRoute);
router.put(
  '/:priority',
  validateParams(priorityParamSchema),
  validateBody(routeBodySchema),
  updateRoute,
);
router.patch(
  '/:priority/swap',
  validateParams(priorityParamSchema),
  validateBody(swapRouteBodySchema),
  swapRoute,
);
router.delete('/:priority', validateParams(priorityParamSchema), deleteRoute);

export default router;
