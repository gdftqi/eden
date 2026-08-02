CREATE DATABASE IF NOT EXISTS `db_eva` DEFAULT CHARACTER SET utf8mb4 DEFAULT COLLATE utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS `db_eva`.`t_user_basic` (
    `f_id`          BIGINT       NOT NULL AUTO_INCREMENT COMMENT '主键',
    `f_username`    VARCHAR(200) NOT NULL                COMMENT '用户名',
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
    `f_id`          BIGINT      NOT NULL COMMENT '',
    `f_nickname`    VARCHAR(16) NOT NULL COMMENT '',
    `f_phone_num`   VARCHAR(16) NOT NULL COMMENT '',
    `f_create_time` BIGINT      NOT NULL COMMENT '',
    `f_state`       TINYINT     NOT NULL COMMENT '',

    PRIMARY KEY pk_id(`f_id`),
    UNIQUE KEY uk_phone_num(`f_phone_num`),
    INDEX nk_create_time(`f_create_time`),
    INDEX nk_state(`f_state`)
) ENGINE = INNODB CHARACTER SET utf8mb4 COMMENT '用户信息表';


CREATE TABLE IF NOT EXISTS `db_eva`.`t_department` (
    `f_id`          BIGINT       NOT NULL AUTO_INCREMENT COMMENT '主键',
    `f_name`        VARCHAR(200) NOT NULL                COMMENT '部门名称',
    `f_manager`     BIGINT       NOT NULL DEFAULT 0      COMMENT '负责人',
    `f_desc`        VARCHAR(200) NOT NULL DEFAULT ''     COMMENT '备注',
    `f_create_time` BIGINT       NOT NULL                COMMENT '创建时间',
    `f_state`       TINYINT      NOT NULL DEFAULT 1      COMMENT '1: 启用; -1: 禁用',

    PRIMARY KEY pk_id(`f_id`),
    UNIQUE KEY uk_name(`f_name`),
    INDEX nk_create_time(`f_create_time`),
    INDEX nk_state(`f_state`)
) ENGINE = INNODB CHARACTER SET utf8mb4 COMMENT '部门信息表';


CREATE TABLE IF NOT EXISTS `db_eva`.`r_user_depart` (
    `f_user_id`   BIGINT  NOT NULL COMMENT '',
    `f_depart_id` BIGINT  NOT NULL COMMENT '',
    `f_state`     TINYINT NOT NULL COMMENT '',

    PRIMARY KEY pk_user_depart_id(`f_user_id`, `f_depart_id`),
    INDEX nk_state(`f_state`)
) ENGINE = INNODB CHARACTER SET utf8mb4 COMMENT '用户和部门关系表';


CREATE TABLE IF NOT EXISTS `db_eva`.`t_permission` (
    `f_id`    BIGINT       NOT NULL AUTO_INCREMENT COMMENT '',
    `f_desc`  VARCHAR(200) NOT NULL                COMMENT '',
    `f_state` TINYINT      NOT NULL                COMMENT '',

    PRIMARY KEY pk_id(`f_id`, `f_depart_id`),
    INDEX nk_state(`f_state`)
) ENGINE = INNODB CHARACTER SET utf8mb4 COMMENT '权限表';


CREATE TABLE IF NOT EXISTS `db_eva`.`t_user_info` (
    `f_id`          BIGINT       NOT NULL COMMENT '主键',
    `f_nickname`    VARCHAR(200) NOT NULL COMMENT '昵称',
    `f_phone_num`   VARCHAR(200) NOT NULL COMMENT '电话号码',
    `f_desc`        VARCHAR(200) NOT NULL COMMENT '备注',
    `f_create_time` BIGINT       NOT NULL COMMENT '创建时间',

    PRIMARY KEY pk_id(`f_id`),
    UNIQUE KEY uk_nickname(`f_nickname`),
    INDEX nk_create_time(`f_create_time`),
    INDEX nk_state(`f_state`)
) ENGINE = INNODB CHARACTER SET utf8mb4 COMMENT '用户信息表';


CREATE TABLE IF NOT EXISTS `db_eva`.`r_user_permission` (
    `f_user_id`       BIGINT  NOT NULL COMMENT '用户id',
    `f_permission_id` BIGINT  NOT NULL COMMENT '权限id',
    `f_state`         TINYINT NOT NULL COMMENT '状态',

    PRIMARY KEY pk_user_permission_id(`f_user_id`, `f_permission_id`),
    INDEX nk_state(`f_state`)
) ENGINE = INNODB CHARACTER SET utf8mb4 COMMENT '用户和权限关系表';



INSERT INTO `db_eva`.`t_user_basic`(`f_username`, `f_password`, `f_create_time`, `f_last_login`, `f_state`)
VALUES ('admin', '$2a$10$obW/1/ZuMl4g/dczG.Qqi.5R0yw4LlRolpAnRO6kBoGBO9ZXRUITC', UNIX_TIMESTAMP(), 0, 1)
ON DUPLICATE KEY UPDATE `f_password` = VALUES(`f_password`), `f_state` = 1;

INSERT INTO `db_eva`.`t_user_info` (`f_id`, `f_nickname`, `f_phone_num`, `f_create_time`, `f_state`)
SELECT `f_id`, '管理员', '', UNIX_TIMESTAMP(), 1
FROM `db_eva`.`t_user_basic` WHERE `f_username` = 'admin'
ON DUPLICATE KEY UPDATE `f_nickname` = VALUES(`f_nickname`), `f_state` = 1;
