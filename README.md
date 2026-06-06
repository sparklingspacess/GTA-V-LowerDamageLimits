# LowerDamageLimits
Patches out clamps in GTA V's damage shaders that prevent deformation on certain parts of entities.

For example, smashing the side of a car will actually deform the side of it, sides of vehicles aren't un-deformable anymore.

I would like to thank Boris Vorontsov for figuring out how to patch this out in the first place, I looked into how ENB did it and made my own implementation of ENB's 'IgnoreDamageLimits' option (with some changes, limits are still there but are lowered)
