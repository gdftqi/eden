CREATE DATABASE IF NOT EXISTS `db_eva` DEFAULT CHARACTER SET utf8mb4 DEFAULT COLLATE utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS `db_eva`.`t_user_basic` (
    `f_id`          BIGINT       NOT NULL AUTO_INCREMENT COMMENT '主键',
    `f_username`    VARCHAR(200) NOT NULL                COMMENT '用户名',
    `f_avatar`      VARCHAR(200) NOT NULL DEFAULT ''     COMMENT '头像, 用来存头像的url',
    `f_password`    CHAR(60)     NOT NULL                COMMENT '密码(bcrypt)',
    `f_create_time` BIGINT       NOT NULL                COMMENT '创建时间',
    `f_last_login`  BIGINT       NOT NULL                COMMENT '最后登录时间',
    `f_state`       TINYINT      NOT NULL DEFAULT 1      COMMENT '1: 启用; -1: 禁用',

    PRIMARY KEY pk_id(`f_id`),
    UNIQUE KEY uk_username(`f_username`),
    INDEX nk_create_time(`f_create_time`),
    INDEX nk_last_login(`f_last_login`),
    INDEX nk_state(`f_state`)
) ENGINE = INNODB CHARACTER SET utf8mb4 COMMENT '用户基础表';


CREATE TABLE IF NOT EXISTS `db_eva`.`t_user_info` (
    `f_id`          BIGINT      NOT NULL              COMMENT '',
    `f_nickname`    VARCHAR(16) NOT NULL              COMMENT '',
    `f_phone_num`   VARCHAR(16) NULL     DEFAULT NULL COMMENT '',
    `f_create_time` BIGINT      NOT NULL              COMMENT '',

    PRIMARY KEY pk_id(`f_id`),
    UNIQUE KEY uk_phone_num(`f_phone_num`),
    INDEX nk_create_time(`f_create_time`)
) ENGINE = INNODB CHARACTER SET utf8mb4 COMMENT '用户信息表';


CREATE TABLE IF NOT EXISTS `db_eva`.`t_department` (
    `f_id`          BIGINT       NOT NULL AUTO_INCREMENT COMMENT '主键',
    `f_name`        VARCHAR(200) NOT NULL                COMMENT '部门名称',
    `f_avatar`      VARCHAR(200) NOT NULL DEFAULT ''     COMMENT '部门头像',
    `f_desc`        VARCHAR(200) NOT NULL DEFAULT ''     COMMENT '备注',
    `f_create_time` BIGINT       NOT NULL                COMMENT '创建时间',
    `f_state`       TINYINT      NOT NULL DEFAULT 1      COMMENT '1: 启用; -1: 禁用',

    PRIMARY KEY pk_id(`f_id`),
    UNIQUE KEY uk_name(`f_name`),
    INDEX nk_create_time(`f_create_time`),
    INDEX nk_state(`f_state`)
) ENGINE = INNODB CHARACTER SET utf8mb4 COMMENT '部门信息表';


CREATE TABLE IF NOT EXISTS `db_eva`.`r_user_depart` (
    `f_user_id`   BIGINT  NOT NULL           COMMENT '',
    `f_depart_id` BIGINT  NOT NULL           COMMENT '',
    `f_state`     TINYINT NOT NULL DEFAULT 1 COMMENT '1: 在部门内; -1: 已移出',

    PRIMARY KEY pk_user_depart_id(`f_user_id`, `f_depart_id`),
    INDEX nk_state(`f_state`)
) ENGINE = INNODB CHARACTER SET utf8mb4 COMMENT '用户和部门关系表';


CREATE TABLE IF NOT EXISTS `db_eva`.`t_permission` (
    `f_id`    BIGINT       NOT NULL AUTO_INCREMENT COMMENT '',
    `f_desc`  VARCHAR(200) NOT NULL                COMMENT '',
    `f_state` TINYINT      NOT NULL                COMMENT '',

    PRIMARY KEY pk_id(`f_id`),
    INDEX nk_state(`f_state`)
) ENGINE = INNODB CHARACTER SET utf8mb4 COMMENT '权限表';


CREATE TABLE IF NOT EXISTS `db_eva`.`r_user_permission` (
    `f_user_id`       BIGINT  NOT NULL COMMENT '用户id',
    `f_permission_id` BIGINT  NOT NULL COMMENT '权限id',
    `f_state`         TINYINT NOT NULL COMMENT '状态',

    PRIMARY KEY pk_user_permission_id(`f_user_id`, `f_permission_id`),
    INDEX nk_state(`f_state`)
) ENGINE = INNODB CHARACTER SET utf8mb4 COMMENT '用户和权限关系表';


INSERT INTO `db_eva`.`t_user_basic`(`f_username`, `f_avatar`, `f_password`, `f_create_time`, `f_last_login`, `f_state`)
VALUES ('admin', '', '$2a$10$obW/1/ZuMl4g/dczG.Qqi.5R0yw4LlRolpAnRO6kBoGBO9ZXRUITC', UNIX_TIMESTAMP(), 0, 1) AS new
ON DUPLICATE KEY UPDATE `f_password` = new.`f_password`, `f_state` = new.`f_state`;


INSERT INTO `db_eva`.`t_user_info` (`f_id`, `f_nickname`, `f_create_time`)
SELECT * FROM (
    SELECT `f_id`,
           '管理员'          AS `f_nickname`,
           UNIX_TIMESTAMP()  AS `f_create_time`
    FROM `db_eva`.`t_user_basic` WHERE `f_username` = 'admin'
) AS new
ON DUPLICATE KEY UPDATE `f_nickname` = new.`f_nickname`;


CREATE OR REPLACE VIEW `db_eva`.`v_user` AS
SELECT b.`f_id`          AS `id`,
       b.`f_avatar`      AS `avatar`,
       b.`f_username`    AS `username`,
       i.`f_nickname`    AS `nickname`,
       i.`f_phone_num`   AS `phone_num`,
       b.`f_create_time` AS `create_time`,
       b.`f_last_login`  AS `last_login`,
       b.`f_state`       AS `state`
FROM `db_eva`.`t_user_basic` AS b
LEFT JOIN `db_eva`.`t_user_info` AS i ON b.`f_id` = i.`f_id`;