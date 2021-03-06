USE Winlogon

DECLARE @bSendmail bit
SET @bSendmail = 1

DECLARE @SCARD_W_WRONG_CHV bigint
SET @SCARD_W_WRONG_CHV = -2146434965

DECLARE @crlf nvarchar(2)
SET @crlf = CHAR(13) + CHAR(10)

DECLARE @MessageBody nvarchar(4000)
SET @MessageBody = ""

DECLARE @Buffer nvarchar(256), @Number nvarchar(5), @Percent nvarchar(3)

DECLARE @Checkdate datetime
SET @Checkdate = DATEADD(day, -8, GETDATE())
-- SET @Checkdate = DATEADD(day, -1, GETDATE())

--
-- Get number of deployed readers
--
DECLARE @ReaderNumbers nvarchar(1000)
SET @ReaderNumbers = "Deployed readers" + @crlf +
                      REPLICATE("-", 45) + @crlf

DECLARE @iReaderHandle int, @stReader nvarchar(64)
SET @iReaderHandle = 0
EXEC #GetReader @stReader OUTPUT, @iReaderHandle OUTPUT

WHILE @stReader <> ""
BEGIN

    SELECT DISTINCT READER, MACHINENAME
    FROM AuthMonitor
    WHERE TIMESTAMP > @Checkdate
    AND READER LIKE @stReader + "%"

    SET @Number = CAST(@@ROWCOUNT AS nvarchar(5))
    IF @Number <> 0
    BEGIN
        EXEC master.dbo.xp_sprintf @Buffer OUTPUT, "   %-34s%5s", @stReader, @Number

        SET @ReaderNumbers = 
            @ReaderNumbers + @Buffer + @crlf
    END
    EXEC #GetReader @stReader OUTPUT, @iReaderHandle OUTPUT
END

--
-- Get number of deployed cards
--
DECLARE @CardNumbers nvarchar(1000)
SET @CardNumbers = "Deployed smart cards" + @crlf +
                    REPLICATE("-", 45) + @crlf

DECLARE @iCardHandle int, @stCard nvarchar(64)
SET @iCardHandle = 0
EXEC #GetCard @stCard OUTPUT, @iCardHandle OUTPUT

WHILE @stCard <> ""
BEGIN

    SELECT DISTINCT CARD, USERNAME
    FROM AuthMonitor
    WHERE TIMESTAMP > @Checkdate
    AND CARD = @stCard

    SET @Number = CAST(@@ROWCOUNT AS nvarchar(5))
    IF @Number <> 0
    BEGIN
        EXEC master.dbo.xp_sprintf @Buffer OUTPUT, "   %-34s%5s", @stCard, @Number

        SET @CardNumbers = 
            @CardNumbers + @Buffer + @crlf
    END
    EXEC #GetCard @stCard OUTPUT, @iCardHandle OUTPUT
END

--
-- Send mail
--
SET @MessageBody = @ReaderNumbers + @crlf +
                   @CardNumbers

IF @bSendmail <> 0
    EXEC master.dbo.xp_sendmail 
         @recipients = 'smcaft', 
         @message =  @MessageBody,
         @subject = 'Smart card self host report - deployment data'
ELSE
    PRINT @MessageBody

GO