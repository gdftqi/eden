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
