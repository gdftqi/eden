package mid

import (
	"context"
	"fmt"
	"io"
	"time"

	"github.com/aws/aws-sdk-go-v2/aws"
	"github.com/aws/aws-sdk-go-v2/credentials"
	"github.com/aws/aws-sdk-go-v2/service/s3"
)

// S3 客户端
var S3 *s3.Client

// S3 超时时间
const S3_TIMEOUT = time.Second * 60

// S3 配置
type S3Config struct {
	AccessKey string `yaml:"access_key"` // S3 access key
	SecretKey string `yaml:"secret_key"` // S3 secret key
	Bucket    string `yaml:"bucket"`     // S3 bucket name
	Region    string `yaml:"region"`     // S3 region
}

var s3Conf *S3Config

func InitS3(c *S3Config) error {
	cli := s3.NewFromConfig(aws.Config{
		Region:      c.Region,
		Credentials: credentials.NewStaticCredentialsProvider(c.AccessKey, c.SecretKey, ""),
	})

	ctx, cancel := context.WithTimeout(context.Background(), S3_TIMEOUT)
	defer cancel()

	_, err := cli.HeadBucket(ctx, &s3.HeadBucketInput{Bucket: aws.String(c.Bucket)})
	if err != nil {
		return err
	}

	S3 = cli
	s3Conf = c
	return nil
}

// S3Put 上传一个对象
func S3Put(key string, r io.Reader, size int64, contentType string) (string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), S3_TIMEOUT)
	defer cancel()

	_, err := S3.PutObject(ctx, &s3.PutObjectInput{
		Bucket:        aws.String(s3Conf.Bucket),
		Key:           aws.String(key),
		Body:          r,
		ContentLength: aws.Int64(size),
		ContentType:   aws.String(contentType),
	})
	if err != nil {
		return "", err
	}

	return S3Url(key), nil
}

// S3Del 删除一个对象
func S3Del(key string) error {
	ctx, cancel := context.WithTimeout(context.Background(), S3_TIMEOUT)
	defer cancel()

	_, err := S3.DeleteObject(ctx, &s3.DeleteObjectInput{
		Bucket: aws.String(s3Conf.Bucket),
		Key:    aws.String(key),
	})
	return err
}

// S3Url 对象的访问地址(virtual-hosted 风格)
func S3Url(key string) string {
	return fmt.Sprintf("https://%s.s3.%s.amazonaws.com/%s", s3Conf.Bucket, s3Conf.Region, key)
}
