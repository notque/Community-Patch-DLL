-- Sword into Plowshare (Now Church)
UPDATE Beliefs
SET CityGrowthModifier = '0'
WHERE Type = 'BELIEF_SWORD_PLOWSHARES' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET RequiresPeace = '0'
WHERE Type = 'BELIEF_SWORD_PLOWSHARES' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

-- Guruship (Now Mandir)
UPDATE Belief_YieldChangeAnySpecialist
SET Yield = '0'
WHERE BeliefType = 'BELIEF_GURUSHIP' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

-- Holy Warriors (Now Synagogue)
DELETE FROM Belief_EraFaithUnitPurchase
WHERE BeliefType = 'BELIEF_HOLY_WARRIORS' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

-- Monasteries (Now Stupa)
UPDATE Belief_BuildingClassFaithPurchase
SET BuildingClassType = 'BUILDINGCLASS_STUPA'
WHERE BeliefType = 'BELIEF_MONASTERIES' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

-- Feed the World (Now Thrift)
UPDATE Belief_BuildingClassYieldChanges
SET YieldChange = '0'
WHERE BeliefType = 'BELIEF_FEED_WORLD' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET HalvedFollowers = '1'
WHERE Type = 'BELIEF_FEED_WORLD' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

-- Peace Gardens (Now Cooperation)
UPDATE Belief_BuildingClassHappiness
SET Happiness = '0'
WHERE BeliefType = 'BELIEF_PEACE_GARDENS' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

-- Choral Music (Now Scholarship)
UPDATE Beliefs
SET MinFollowers = '0'
WHERE Type = 'BELIEF_CHORAL_MUSIC' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Belief_BuildingClassYieldChanges
SET YieldChange = '0'
WHERE BeliefType = 'BELIEF_CHORAL_MUSIC' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET HalvedFollowers = '1'
WHERE Type = 'BELIEF_CHORAL_MUSIC' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

-- Religious Art (Now Mastery)
UPDATE Belief_BuildingClassYieldChanges
SET YieldChange = '0'
WHERE BeliefType = 'BELIEF_RELIGIOUS_ART' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Belief_BuildingClassTourism
SET Tourism = '0'
WHERE BeliefType = 'BELIEF_RELIGIOUS_ART' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

-- Liturgical Drama (Now Veneration)
UPDATE Belief_BuildingClassYieldChanges
SET YieldChange = '0'
WHERE BeliefType = 'BELIEF_LITURGICAL_DRAMA' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET MinFollowers = '0'
WHERE Type = 'BELIEF_LITURGICAL_DRAMA' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET HalvedFollowers = '1'
WHERE Type = 'BELIEF_LITURGICAL_DRAMA' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

-- Asceticism (Now Asceticism)
UPDATE Beliefs
SET MinFollowers = '0'
WHERE Type = 'BELIEF_ASCETISM' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Belief_BuildingClassHappiness
SET Happiness = '0'
WHERE BeliefType = 'BELIEF_ASCETISM' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET HalvedFollowers = '1'
WHERE Type = 'BELIEF_ASCETISM' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

-- Religious Center (Now Order)
UPDATE Belief_BuildingClassHappiness
SET Happiness = '0'
WHERE BeliefType = 'BELIEF_RELIGIOUS_CENTER' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET MinFollowers = '0'
WHERE Type = 'BELIEF_RELIGIOUS_CENTER' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

-- Religious Community (Now Diliegence)
UPDATE Beliefs
SET HalvedFollowers = '1'
WHERE Type = 'BELIEF_RELIGIOUS_COMMUNITY' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

DELETE FROM Belief_MaxYieldModifierPerFollower
WHERE BeliefType = 'BELIEF_RELIGIOUS_COMMUNITY' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

-- Divine Inspiration (Now Inspiration)
UPDATE Belief_YieldChangeWorldWonder
SET Yield = '0'
WHERE BeliefType = 'BELIEF_DIVINE_INSPIRATION' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET HalvedFollowers = '1'
WHERE Type = 'BELIEF_DIVINE_INSPIRATION' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

-- NEW

INSERT INTO Belief_SpecialistYieldChanges
	(BeliefType, SpecialistType, YieldType, Yield)
VALUES
	('BELIEF_RELIGIOUS_ART', 'SPECIALIST_MERCHANT', 'YIELD_GOLD', 2),
	('BELIEF_RELIGIOUS_ART', 'SPECIALIST_SCIENTIST', 'YIELD_SCIENCE', 2),
	('BELIEF_RELIGIOUS_ART', 'SPECIALIST_ENGINEER', 'YIELD_PRODUCTION', 2),
	('BELIEF_RELIGIOUS_ART', 'SPECIALIST_ARTIST', 'YIELD_CULTURE', 2),
	('BELIEF_RELIGIOUS_ART', 'SPECIALIST_WRITER', 'YIELD_CULTURE', 2),
	('BELIEF_RELIGIOUS_ART', 'SPECIALIST_MUSICIAN', 'YIELD_CULTURE', 2);

INSERT INTO Belief_MaxYieldPerFollower
	(BeliefType, YieldType, Max)
VALUES
	('BELIEF_RELIGIOUS_COMMUNITY', 'YIELD_PRODUCTION', 20),
	('BELIEF_FEED_WORLD', 'YIELD_GOLD', 20),
	('BELIEF_CHORAL_MUSIC', 'YIELD_SCIENCE', 15),
	('BELIEF_ASCETISM', 'YIELD_FOOD', 15),
	('BELIEF_LITURGICAL_DRAMA', 'YIELD_FAITH', 10),
	('BELIEF_DIVINE_INSPIRATION', 'YIELD_CULTURE', 10);

INSERT INTO Belief_YieldPerBirth
	(BeliefType, YieldType, Yield)
VALUES
	('BELIEF_PEACE_GARDENS', 'YIELD_FAITH', 5),
	('BELIEF_PEACE_GARDENS', 'YIELD_GOLD', 5),
	('BELIEF_PEACE_GARDENS', 'YIELD_CULTURE', 5),
	('BELIEF_PEACE_GARDENS', 'YIELD_PRODUCTION', 5),
	('BELIEF_PEACE_GARDENS', 'YIELD_FOOD', 5),
	('BELIEF_PEACE_GARDENS', 'YIELD_SCIENCE', 5);

INSERT INTO Belief_BuildingClassFaithPurchase
	(BeliefType, BuildingClassType)
VALUES
	('BELIEF_SWORD_PLOWSHARES', 'BUILDINGCLASS_CHURCH'),
	('BELIEF_GURUSHIP', 'BUILDINGCLASS_MANDIR'),
	('BELIEF_HOLY_WARRIORS', 'BUILDINGCLASS_SYNAGOGUE'),
	('BELIEF_RELIGIOUS_CENTER', 'BUILDINGCLASS_ORDER');

UPDATE Beliefs
SET Tooltip = 'TXT_KEY_BUILDING_CATHEDRAL_TOOLTIP'
WHERE Type = 'BELIEF_CATHEDRALS' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET Tooltip = 'TXT_KEY_BUILDING_PAGODA_TOOLTIP'
WHERE Type = 'BELIEF_PAGODAS' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET Tooltip = 'TXT_KEY_BUILDING_MOSQUE_TOOLTIP'
WHERE Type = 'BELIEF_MOSQUES' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET Tooltip = 'TXT_KEY_BUILDING_STUPA_TOOLTIP'
WHERE Type = 'BELIEF_MONASTERIES' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET Tooltip = 'TXT_KEY_BUILDING_SYNAGOGUE_TOOLTIP'
WHERE Type = 'BELIEF_HOLY_WARRIORS' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET Tooltip = 'TXT_KEY_BUILDING_MANDIR_TOOLTIP'
WHERE Type = 'BELIEF_GURUSHIP' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET Tooltip = 'TXT_KEY_BUILDING_CHURCH_TOOLTIP'
WHERE Type = 'BELIEF_SWORD_PLOWSHARES' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET Tooltip = 'TXT_KEY_BUILDING_ORDER_TOOLTIP'
WHERE Type = 'BELIEF_RELIGIOUS_CENTER' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

-- Rest of Tooltips (for LUA)

UPDATE Beliefs
SET Tooltip = 'TXT_KEY_BELIEF_FEED_WORLD'
WHERE Type = 'BELIEF_FEED_WORLD' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET Tooltip = 'TXT_KEY_BELIEF_PEACE_GARDENS'
WHERE Type = 'BELIEF_PEACE_GARDENS' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET Tooltip = 'TXT_KEY_BELIEF_CHORAL_MUSIC'
WHERE Type = 'BELIEF_CHORAL_MUSIC' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET Tooltip = 'TXT_KEY_BELIEF_CHORAL_MUSIC'
WHERE Type = 'BELIEF_CHORAL_MUSIC' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET Tooltip = 'TXT_KEY_BELIEF_RELIGIOUS_ART'
WHERE Type = 'BELIEF_RELIGIOUS_ART' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET Tooltip = 'TXT_KEY_BELIEF_LITURGICAL_DRAMA'
WHERE Type = 'BELIEF_LITURGICAL_DRAMA' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET Tooltip = 'TXT_KEY_BELIEF_ASCETISM'
WHERE Type = 'BELIEF_ASCETISM' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET Tooltip = 'TXT_KEY_BELIEF_RELIGIOUS_COMMUNITY'
WHERE Type = 'BELIEF_RELIGIOUS_COMMUNITY' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );

UPDATE Beliefs
SET Tooltip = 'TXT_KEY_BELIEF_DIVINE_INSPIRATION'
WHERE Type = 'BELIEF_DIVINE_INSPIRATION' AND EXISTS (SELECT * FROM COMMUNITY WHERE Type='COMMUNITY_CORE_BALANCE_RELIGION' AND Value= 1 );
