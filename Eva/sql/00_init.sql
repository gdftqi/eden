CREATE DATABASE IF NOT EXISTS `db_eva` DEFAULT CHARACTER SET utf8mb4 DEFAULT COLLATE utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS `db_eva`.`t_user_basic` (
    `f_id`          BIGINT       NOT NULL AUTO_INCREMENT COMMENT '主键',
    `f_username`    VARCHAR(200) NOT NULL                COMMENT '用户名',
    `f_password`    CHAR(64)     NOT NULL                COMMENT '密码',
    `f_create_time` BIGINT       NOT NULL                COMMENT '创建时间',
    `f_state`       TINYINT      NOT NULL DEFAULT 1      COMMENT '1: 启用; 0: 禁用',

    PRIMARY KEY pk_id(`f_id`),
    UNIQUE KEY uk_username(`f_username`),
    INDEX ndx_create_time(`f_create_time`),
    INDEX ndx_state(`f_state`)
) ENGINE = MYISAM CHARACTER SET utf8mb4;
